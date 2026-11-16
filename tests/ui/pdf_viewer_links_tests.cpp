// The PDF viewer's links, Back and Forward, and its Outline (docs/pdf-viewer.md).

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#if defined(PATCHY_HAVE_QT_PDF)

#include "import_print_resolution_test_support.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/modifier_names.hpp"
#include "ui/pdf_links.hpp"
#include "ui/pdf_page_view.hpp"
#include "ui/pdf_text_selection.hpp"
#include "ui/pdf_viewer_window.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFile>
#include <QHelpEvent>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>

#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using patchy::test::ui::process_events_until;
using patchy::test::ui::send_mouse;
using patchy::test::ui::SettingsValueRestorer;
using patchy::test::ui::show_window;
using patchy::test::ui::import_print_resolution_support::pdf_from_objects;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::PdfPageLink;
using patchy::ui::PdfPageView;
using patchy::ui::PdfViewerWindow;

namespace {

constexpr double kPageWidth = 612.0;
constexpr double kPageHeight = 792.0;
constexpr int kPages = 6;
// The margin the page view keeps above a place it jumps to (PageViewLayoutOptions::margin).
constexpr double kJumpMargin = 16.0;
// Where the "3. Masks" link and the "Painting on a mask" outline entry land on page 4, in
// points from the page's top left. A link on that page starts at exactly this point.
constexpr double kMasksSpotX = 72.0;
constexpr double kMasksSpotY = 392.0;

const char* const kWebAddressText = "https://github.com/SethRobinson/Patchy";
const char* const kMailAddressText = "mailto:someone@example.com";
const char* const kFileAddressText = "file:///C:/Windows/notepad.exe";
const QString kWebAddress = QString::fromLatin1(kWebAddressText);
const QString kMailAddress = QString::fromLatin1(kMailAddressText);
const QString kFileAddress = QString::fromLatin1(kFileAddressText);

std::string number(double value) {
  return QString::number(value, 'f', 2).toStdString();
}

int page_object(int page) {
  return 4 + 2 * page;
}

struct PageParts {
  std::string content;
  std::string annotations;
};

void add_text(PageParts& page, double x, double y, double size, const std::string& text, bool link_color = false) {
  page.content += std::string(link_color ? "0 0 0.85 rg " : "0 0 0 rg ") + "BT /F1 " + number(size) + " Tf 1 0 0 1 " +
                  number(x) + " " + number(y) + " Tm (" + text + ") Tj ET\n";
}

// A line of link-colored text with a link over it: 4 points under the baseline to 12 over.
void add_link(PageParts& page, double x, double y, const std::string& text, const std::string& action) {
  add_text(page, x, y, 12.0, text, true);
  const double width = 6.4 * static_cast<double>(text.size());
  page.annotations += "<</Type/Annot/Subtype/Link/Border[0 0 0]/Rect[" + number(x) + " " + number(y - 4.0) + " " +
                      number(x + width) + " " + number(y + 12.0) + "]" + action + ">>";
}

std::string destination(int page, const std::string& how) {
  return "/Dest[" + std::to_string(page_object(page)) + " 0 R/" + how + "]";
}

std::string address(const std::string& uri) {
  return "/A<</S/URI/URI(" + uri + ")>>";
}

struct OutlineNode {
  std::string title;
  std::string target;
  std::vector<OutlineNode> children;
  int id{0};
};

void number_outline(std::vector<OutlineNode>& nodes, int& next) {
  for (auto& node : nodes) {
    node.id = next++;
    number_outline(node.children, next);
  }
}

void write_outline(const std::vector<OutlineNode>& nodes, int parent, std::map<int, std::string>& objects) {
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const auto& node = nodes[index];
    std::string object = "<</Title(" + node.title + ")/Parent " + std::to_string(parent) + " 0 R";
    if (index > 0) {
      object += "/Prev " + std::to_string(nodes[index - 1].id) + " 0 R";
    }
    if (index + 1 < nodes.size()) {
      object += "/Next " + std::to_string(nodes[index + 1].id) + " 0 R";
    }
    if (!node.children.empty()) {
      object += "/First " + std::to_string(node.children.front().id) + " 0 R/Last " +
                std::to_string(node.children.back().id) + " 0 R/Count " + std::to_string(node.children.size());
    }
    objects[node.id] = object + node.target + ">>";
    write_outline(node.children, node.id, objects);
  }
}

// Six US Letter pages: a contents page whose lines link to the chapters (one to a point in
// the middle of a page, one with no point), links to a web address, a mail address, a file,
// a script address and a program, a script action (Qt PDF reports none there), an address
// typed as plain text, a "Back to contents" link on every other page, and a nested outline.
// With `lone_root` the outline has one entry at the top. The same file is the desk's test
// file and the card's picture.
QByteArray links_pdf_bytes(bool lone_root = false) {
  std::vector<PageParts> pages(static_cast<std::size_t>(kPages));
  const std::string top = "XYZ 0 " + number(kPageHeight) + " 0";
  {
    auto& page = pages[0];
    add_text(page, 72.0, 700.0, 20.0, "A Short Guide to Layers");
    add_text(page, 72.0, 650.0, 13.0, "Contents");
    add_link(page, 72.0, 620.0, "1. What a layer is", destination(1, top));
    add_link(page, 72.0, 596.0, "2. Opacity and blending", destination(2, top));
    add_link(page, 72.0, 572.0, "3. Masks: painting on a mask",
             destination(3, "XYZ " + number(kMasksSpotX) + " " + number(kPageHeight - kMasksSpotY) + " 0"));
    add_link(page, 72.0, 548.0, "4. Groups", destination(4, "Fit"));
    add_link(page, 72.0, 524.0, "5. Saving your work", destination(5, top));
    add_text(page, 72.0, 470.0, 13.0, "Links to try");
    add_link(page, 72.0, 440.0, "Patchy on GitHub", address(kWebAddressText));
    add_link(page, 72.0, 416.0, "Write to someone", address(kMailAddressText));
    add_link(page, 72.0, 392.0, "A file on this computer", address(kFileAddressText));
    add_link(page, 72.0, 368.0, "A script address", address("javascript:alert\\(1\\)"));
    add_link(page, 72.0, 344.0, "A program to start", "/A<</S/Launch/F(calc.exe)>>");
    // A script action: drawn like plain text, because no viewer should make it a link.
    add_text(page, 72.0, 320.0, 12.0, "A script action");
    page.annotations += "<</Type/Annot/Subtype/Link/Border[0 0 0]/Rect[72 316 200 332]"
                        "/A<</S/JavaScript/JS(app.beep\\(0\\))>>>>";
    add_text(page, 72.0, 270.0, 12.0, "A typed address: https://example.com/guide?topic=layers");
  }
  const std::vector<std::pair<std::string, std::vector<std::string>>> chapters = {
      {"1. What a layer is",
       {"Every picture in an editor is a stack of layers. A layer is a sheet of",
        "pixels, and the sheets are laid one over another like panes of glass.",
        "What you see on the canvas is the whole stack looked at from above."}},
      {"2. Opacity and blending",
       {"Each layer has an opacity. At 100 percent the layer hides what is under",
        "it. At 50 percent the layer below shows through, half and half.",
        "A blend mode decides how a layer mixes with the layers below."}},
      {"3. Masks",
       {"A mask hides part of a layer without erasing it. Paint black on the mask",
        "and that part of the layer disappears. Paint white and it returns.",
        "The pixels are still there, behind the mask."}},
      {"4. Groups",
       {"A long stack is hard to read, so layers can be put in groups. A group is",
        "a folder in the Layers panel. Close the folder and its layers take one row."}},
      {"5. Saving your work",
       {"Save the picture with its layers while you are still working on it. A",
        "layered file keeps every layer, mask, group and adjustment."}},
  };
  for (int chapter = 0; chapter < static_cast<int>(chapters.size()); ++chapter) {
    auto& page = pages[static_cast<std::size_t>(chapter + 1)];
    add_text(page, 72.0, 700.0, 20.0, chapters[static_cast<std::size_t>(chapter)].first);
    double y = 660.0;
    for (const auto& line : chapters[static_cast<std::size_t>(chapter)].second) {
      add_text(page, 72.0, y, 12.0, line);
      y -= 18.0;
    }
    if (chapter + 1 == 3) {
      // The point the "3. Masks" link names is this link's top left corner.
      add_text(page, 72.0, kPageHeight - kMasksSpotY + 22.0, 13.0, "Painting on a mask");
      add_link(page, kMasksSpotX, kPageHeight - kMasksSpotY - 12.0, "Back to contents", destination(0, top));
      add_text(page, 72.0, kPageHeight - kMasksSpotY - 36.0, 12.0,
               "A soft brush gives the join a soft edge. Unmask a corner today, mask it again tomorrow.");
    }
    add_link(page, 72.0, 100.0, "Back to contents", destination(0, top));
  }

  std::vector<OutlineNode> outline = {
      {"A Short Guide to Layers",
       destination(0, top),
       {{"1. What a layer is", destination(1, top), {}},
        {"2. Opacity and blending", destination(2, top), {}},
        {"3. Masks",
         destination(3, top),
         {{"Painting on a mask",
           destination(3, "XYZ " + number(kMasksSpotX) + " " + number(kPageHeight - kMasksSpotY) + " 0"),
           {}}}},
        {"4. Groups", destination(4, "Fit"), {}}}},
  };
  if (!lone_root) {
    outline.push_back({"5. Saving your work", destination(5, top), {}});
    outline.push_back({"Patchy on GitHub", address(kWebAddressText), {}});
  }
  const int outline_root = page_object(kPages);
  int next = outline_root + 1;
  number_outline(outline, next);
  std::map<int, std::string> outline_objects;
  write_outline(outline, outline_root, outline_objects);

  std::vector<std::string> objects;
  objects.push_back("<</Type/Catalog/Pages 2 0 R/Outlines " + std::to_string(outline_root) + " 0 R>>");
  std::string kids;
  for (int page = 0; page < kPages; ++page) {
    kids += std::to_string(page_object(page)) + " 0 R ";
  }
  objects.push_back("<</Type/Pages/Kids[" + kids + "]/Count " + std::to_string(kPages) + ">>");
  objects.emplace_back("<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>");
  for (int page = 0; page < kPages; ++page) {
    const auto& parts = pages[static_cast<std::size_t>(page)];
    objects.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 " + number(kPageWidth) + " " + number(kPageHeight) +
                      "]/Resources<</Font<</F1 3 0 R>>>>/Annots[" + parts.annotations + "]/Contents " +
                      std::to_string(page_object(page) + 1) + " 0 R>>");
    objects.push_back("<</Length " + std::to_string(parts.content.size()) + ">>\nstream\n" + parts.content +
                      "\nendstream");
  }
  objects.push_back("<</Type/Outlines/First " + std::to_string(outline.front().id) + " 0 R/Last " +
                    std::to_string(outline.back().id) + " 0 R/Count " + std::to_string(outline.size()) + ">>");
  for (const auto& [id, object] : outline_objects) {
    objects.push_back(object);
  }
  return pdf_from_objects(objects);
}

// Two blank pages and no outline.
QByteArray plain_pdf_bytes() {
  return pdf_from_objects({"<</Type/Catalog/Pages 2 0 R>>", "<</Type/Pages/Kids[3 0 R 4 0 R]/Count 2>>",
                           "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 300]>>",
                           "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 300]>>"});
}

QString write_file(const QString& path, const QByteArray& bytes) {
  QFile::remove(path);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  file.write(bytes);
  return path;
}

// The settings a test here may change, put back when it ends.
struct ViewerSettingsGuard {
  SettingsValueRestorer pages{QStringLiteral("pdfViewer/pages")};
  SettingsValueRestorer continuous{QStringLiteral("pdfViewer/continuous")};
  SettingsValueRestorer direction{QStringLiteral("pdfViewer/direction")};
  SettingsValueRestorer zoom{QStringLiteral("pdfViewer/zoom")};
  SettingsValueRestorer sidebar{QStringLiteral("pdfViewer/sidebar")};
  SettingsValueRestorer sidebar_view{QStringLiteral("pdfViewer/sidebarView")};
  SettingsValueRestorer geometry{QStringLiteral("pdfViewer/geometry")};
  SettingsValueRestorer positions{QStringLiteral("pdfViewer/positions")};
  ViewerSettingsGuard() {
    auto settings = patchy::ui::app_settings();
    for (const char* key : {"pdfViewer/pages", "pdfViewer/continuous", "pdfViewer/direction", "pdfViewer/zoom",
                            "pdfViewer/sidebar", "pdfViewer/sidebarView", "pdfViewer/geometry",
                            "pdfViewer/positions"}) {
      settings.remove(QLatin1String(key));
    }
  }
};

// What the viewer was asked to open and to copy, instead of a browser and the clipboard.
struct LinkLog {
  QList<QUrl> opened;
  QStringList copied;
};

PdfViewerWindow* open_viewer(patchy::ui::MainWindow& window, const QString& path, LinkLog* log = nullptr,
                             QSize size = QSize(900, 700)) {
  auto* viewer = PdfViewerWindow::open(&window, path, {});
  CHECK(viewer != nullptr);
  if (viewer != nullptr) {
    viewer->set_link_handlers(
        [log](const QUrl& url) {
          if (log != nullptr) {
            log->opened.append(url);
          }
          return true;
        },
        [log](const QString& text) {
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

// Waits until the pages are rendered, the links and the text areas of the pages on screen
// are read and the text selection has caught up.
void settle(PdfViewerWindow& viewer) {
  CHECK(process_events_until(
      [&viewer] {
        auto* view = viewer.page_view();
        return !view->render_pending() && !view->links()->busy() && !view->text_selection()->busy();
      },
      15000));
}

struct LinkSpot {
  PdfPageLink link;
  QRectF rect;  // viewport coordinates
  [[nodiscard]] QPoint center() const { return rect.center().toPoint(); }
};

std::vector<LinkSpot> link_spots(PdfPageView& view, int page) {
  std::vector<LinkSpot> spots;
  const auto* links = view.links()->page_links(page);
  const auto rects = view.link_rects_for_testing(page);
  if (links == nullptr || rects.size() != links->size()) {
    return spots;
  }
  for (std::size_t index = 0; index < links->size(); ++index) {
    spots.push_back({(*links)[index], rects[index]});
  }
  return spots;
}

// The first link on `page` that goes to `target_page`.
LinkSpot spot_to_page(PdfPageView& view, int page, int target_page) {
  for (const auto& spot : link_spots(view, page)) {
    if (spot.link.internal() && spot.link.target.page == target_page) {
      return spot;
    }
  }
  std::cout << "  no link from page " << page + 1 << " to page " << target_page + 1 << '\n';
  CHECK(false);
  return {};
}

// The first link on `page` whose address starts with `start`.
LinkSpot spot_with_address(PdfPageView& view, int page, const QString& start) {
  for (const auto& spot : link_spots(view, page)) {
    if (!spot.link.internal() && patchy::ui::pdf_link_address(spot.link.url).startsWith(start)) {
      return spot;
    }
  }
  std::cout << "  no link on page " << page + 1 << " to " << start.toStdString() << '\n';
  CHECK(false);
  return {};
}

// Scrolls so `spot` is inside the page area, a little in from its top left corner, when it
// is not on screen whole.
void bring_into_view(PdfPageView& view, const LinkSpot& spot) {
  if (QRectF(view.viewport()->rect()).contains(spot.rect)) {
    return;
  }
  auto* horizontal = view.horizontalScrollBar();
  auto* vertical = view.verticalScrollBar();
  horizontal->setValue(horizontal->value() + static_cast<int>(std::lround(spot.rect.left())) - 40);
  vertical->setValue(vertical->value() + static_cast<int>(std::lround(spot.rect.top())) - 40);
  QApplication::processEvents();
}

void hover(PdfPageView& view, QPoint position) {
  send_mouse(*view.viewport(), QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton);
}

void click(PdfPageView& view, QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  send_mouse(*view.viewport(), QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(*view.viewport(), QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, modifiers);
}

bool pointing_hand(const PdfPageView& view) {
  return view.viewport()->testAttribute(Qt::WA_SetCursor) &&
         view.viewport()->cursor().shape() == Qt::PointingHandCursor;
}

// The tooltip the page area shows for a pointer resting at `position` (empty: none).
QString tip_at(PdfPageView& view, QPoint position) {
  QHelpEvent event(QEvent::ToolTip, position, view.viewport()->mapToGlobal(position));
  QApplication::sendEvent(view.viewport(), &event);
  QApplication::processEvents();
  return QToolTip::isVisible() ? QToolTip::text() : QString();
}

void send_wheel(QWidget* target, int delta) {
  const QPointF position(target->width() / 2.0, target->height() / 2.0);
  QWheelEvent event(position, target->mapToGlobal(position), QPoint(), QPoint(0, delta), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(target, &event);
}

struct LinkMenu {
  bool shown{false};
  QStringList actions;
  QStringList texts;
  QList<bool> enabled;
  [[nodiscard]] bool action_enabled(const QString& name) const {
    const auto index = actions.indexOf(name);
    return index >= 0 && enabled[index];
  }
};

// Right-clicks the page area at `position` and picks `pick` (an action objectName; empty
// closes the menu) from the page menu, if one opens. A separator is listed with no name.
LinkMenu right_click(PdfPageView& view, QPoint position, const QString& pick = {}) {
  LinkMenu seen;
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
        seen.texts.push_back(action->text());
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

bool link_colored(QRgb pixel) {
  return qBlue(pixel) - std::max(qRed(pixel), qGreen(pixel)) > 60;
}

// Of the link-colored pixels the page area shows, how many there are and how many lie
// inside the link areas of `pages`. Every link in the fixture is drawn in the link color
// and nothing else is, so the two match when the areas sit on their text.
std::pair<int, int> link_color_coverage(PdfPageView& view, const std::vector<int>& pages) {
  const QImage image = view.viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
  const double scale = image.width() / std::max(1.0, static_cast<double>(view.viewport()->width()));
  std::vector<QRectF> areas;
  for (const int page : pages) {
    for (const auto& spot : link_spots(view, page)) {
      areas.push_back(spot.rect.adjusted(-2.0, -2.0, 2.0, 2.0));
    }
  }
  int total = 0;
  int inside = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (!link_colored(image.pixel(x, y))) {
        continue;
      }
      ++total;
      const QPointF point((x + 0.5) / scale, (y + 0.5) / scale);
      for (const auto& area : areas) {
        if (area.contains(point)) {
          ++inside;
          break;
        }
      }
    }
  }
  return {total, inside};
}

void expect_links_on_their_text(const char* what, PdfViewerWindow& viewer) {
  settle(viewer);
  auto* view = viewer.page_view();
  const auto [total, inside] = link_color_coverage(*view, view->current_pages());
  if (total < 150 || inside < total * 0.9) {
    std::cout << "  " << what << ": " << inside << " of " << total << " link-colored pixels are inside a link area\n";
    CHECK(false);
  }
}

// --- Links ---

// What the fixture's links are read as: where each goes, which addresses may open.
void ui_pdf_viewer_links_are_read_from_the_pdf() {
  ViewerSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_links_read.pdf"), links_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  view->set_continuous(false);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  settle(*viewer);
  const auto spots = link_spots(*view, 0);
  std::vector<int> targets;
  QStringList addresses;
  for (const auto& spot : spots) {
    CHECK(view->viewport()->rect().contains(spot.rect.toAlignedRect()));
    if (spot.link.internal()) {
      targets.push_back(spot.link.target.page);
    } else {
      addresses << patchy::ui::pdf_link_address(spot.link.url);
    }
  }
  CHECK(targets == std::vector<int>({1, 2, 3, 4, 5}));
  if (addresses.size() != 6) {
    std::cout << "  addresses: " << addresses.join(QStringLiteral(" | ")).toStdString() << '\n';
  }
  // The script action is not among them; the address typed in the text is.
  CHECK(addresses.size() == 6);
  if (addresses.size() == 6) {
    CHECK(addresses[0] == kWebAddress);
    CHECK(addresses[1] == kMailAddress);
    CHECK(addresses[2] == kFileAddress);
    CHECK(addresses[3].startsWith(QStringLiteral("javascript:")));
    CHECK(addresses[4].startsWith(QStringLiteral("file:")) && addresses[4].contains(QStringLiteral("calc.exe")));
    CHECK(addresses[5] == QStringLiteral("https://example.com/guide?topic=layers"));
  }
  // The point a link names is kept; a link without one goes to the top of its page.
  const auto masks = spot_to_page(*view, 0, 3).link.target;
  CHECK(std::abs(masks.location.x() - kMasksSpotX) < 0.5 && std::abs(masks.location.y() - kMasksSpotY) < 0.5);
  CHECK(spot_to_page(*view, 0, 1).link.target.location.manhattanLength() < 0.5);
  CHECK(spot_to_page(*view, 0, 4).link.target.location.manhattanLength() < 0.5);
  // Only web and mail addresses ever open.
  using patchy::ui::pdf_link_url_opens;
  CHECK(pdf_link_url_opens(QUrl(QStringLiteral("https://example.com/a?b=c"))));
  CHECK(pdf_link_url_opens(QUrl(QStringLiteral("HTTP://Example.com"))));
  CHECK(pdf_link_url_opens(QUrl(QStringLiteral("mailto:someone@example.com"))));
  for (const char* refused : {"file:///C:/Windows/notepad.exe", "file:calc.exe", "javascript:alert(1)", "calc.exe",
                              "ftp://example.com/x", "data:text/html,hello", "vbscript:x", "ms-settings:display",
                              "https:", "http:///nohost", "mailto:", "page2.html", "//example.com/x", ""}) {
    if (pdf_link_url_opens(QUrl(QString::fromLatin1(refused)))) {
      std::cout << "  would open: " << refused << '\n';
      CHECK(false);
    }
  }
  close_viewer(viewer);
}

// The pointer over a link: a pointing hand and a tooltip naming where it goes. A plain click
// follows a link into the document; an address opens only with Ctrl, and only a web or mail
// address.
void ui_pdf_viewer_links_hover_and_click() {
  ViewerSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_links_click.pdf"), links_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  LinkLog log;
  auto* viewer = open_viewer(window, path, &log);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  view->set_continuous(false);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  settle(*viewer);
  const auto chapter = spot_to_page(*view, 0, 1);
  const auto web = spot_with_address(*view, 0, kWebAddress);
  const auto mail = spot_with_address(*view, 0, QStringLiteral("mailto:"));
  const auto file = spot_with_address(*view, 0, kFileAddress);
  const auto script = spot_with_address(*view, 0, QStringLiteral("javascript:"));
  const auto program = spot_with_address(*view, 0, QStringLiteral("file:calc"));
  const QPoint blank(view->viewport()->width() / 2, view->viewport()->height() - 40);
  const QString ctrl_hint = patchy::ui::ctrl_key_name() + QStringLiteral("-click to open the link");

  // Hover: the hand over links only, and the target in the tooltip.
  CHECK(!pointing_hand(*view));
  hover(*view, chapter.center());
  CHECK(pointing_hand(*view));
  CHECK(tip_at(*view, chapter.center()) == QStringLiteral("Page 2"));
  hover(*view, blank);
  CHECK(!pointing_hand(*view));
  (void)tip_at(*view, blank);
  CHECK(process_events_until([] { return !QToolTip::isVisible(); }, 2000));
  hover(*view, web.center());
  CHECK(pointing_hand(*view));
  CHECK(tip_at(*view, web.center()) == QString(kWebAddress + QLatin1Char('\n') + ctrl_hint));
  hover(*view, file.center());
  CHECK(pointing_hand(*view));
  const QString file_tip = tip_at(*view, file.center());
  CHECK(file_tip.startsWith(kFileAddress) &&
        file_tip.contains(QStringLiteral("Patchy does not open this kind of link")));
  // The script action, one line under the last link, is no link at all.
  const QPoint script_action(program.center().x(), 2 * program.center().y() - script.center().y());
  hover(*view, script_action);
  CHECK(view->link_at(QPointF(script_action)) == nullptr && !pointing_hand(*view));

  // A plain click on a web link opens nothing and says how to open it.
  click(*view, web.center());
  CHECK(log.opened.isEmpty());
  CHECK(QToolTip::isVisible() && QToolTip::text().endsWith(ctrl_hint));
  CHECK(view->current_page() == 0 && !viewer->can_go_back());
  // Ctrl+click opens web and mail addresses, each once.
  click(*view, web.center(), Qt::ControlModifier);
  CHECK(log.opened == QList<QUrl>({QUrl(kWebAddress)}));
  click(*view, mail.center(), Qt::ControlModifier);
  CHECK(log.opened.size() == 2 && log.opened.value(1) == QUrl(kMailAddress));
  // Nothing else opens, with Ctrl or without.
  for (const auto* spot : {&file, &script, &program}) {
    click(*view, spot->center(), Qt::ControlModifier);
    click(*view, spot->center());
  }
  CHECK(log.opened.size() == 2);
  CHECK(QToolTip::isVisible() && QToolTip::text().contains(QStringLiteral("Patchy does not open this kind of link")));
  // A second click right behind a click on a link is not a double-click for full screen.
  send_mouse(*view->viewport(), QEvent::MouseButtonDblClick, web.center(), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*view->viewport(), QEvent::MouseButtonRelease, web.center(), Qt::LeftButton, Qt::NoButton);
  CHECK(!viewer->full_screen_mode());
  CHECK(log.opened.size() == 2);

  // A press on a link that turns into a drag selects the link's text (to the line the
  // pointer went down to) and follows nothing.
  send_mouse(*view->viewport(), QEvent::MouseButtonPress, chapter.center(), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*view->viewport(), QEvent::MouseMove, chapter.center() + QPoint(0, 40), Qt::NoButton, Qt::LeftButton);
  send_mouse(*view->viewport(), QEvent::MouseButtonRelease, chapter.center() + QPoint(0, 40), Qt::LeftButton,
             Qt::NoButton);
  CHECK(view->current_page() == 0 && !viewer->can_go_back());
  settle(*viewer);
  CHECK(view->has_text_selection());
  CHECK(view->selected_text().contains(QLatin1Char('\n')));
  // A plain click beside the text drops the selection.
  click(*view, blank);
  CHECK(!view->has_text_selection());

  // A plain click on a link into the document goes there; Back returns.
  click(*view, chapter.center());
  CHECK(view->current_page() == 1);
  CHECK(viewer->can_go_back() && !viewer->can_go_forward());
  // The page under the pointer changed: a second click right behind is still no double-click.
  send_mouse(*view->viewport(), QEvent::MouseButtonDblClick, blank, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*view->viewport(), QEvent::MouseButtonRelease, blank, Qt::LeftButton, Qt::NoButton);
  CHECK(!viewer->full_screen_mode());
  viewer->go_back();
  CHECK(view->current_page() == 0 && viewer->can_go_forward());
  // Forward again, and the link at the foot of page 2 back to the contents.
  settle(*viewer);
  viewer->go_forward();
  settle(*viewer);
  CHECK(view->current_page() == 1);
  click(*view, spot_to_page(*view, 1, 0).center());
  CHECK(view->current_page() == 0);
  close_viewer(viewer);
}

// Jumps land on the point the link names, in every layout and after a rotation.
void ui_pdf_viewer_links_jump_in_every_layout() {
  ViewerSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_links_layouts.pdf"), links_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  // Clicks the contents line for "3. Masks" and checks the row shown and, when the view can
  // scroll there, that the point named sits at the top of the window.
  const auto follow_masks = [&](const char* what, bool expect_spot, int turns = 0) {
    view->first_page();
    settle(*viewer);
    bring_into_view(*view, spot_to_page(*view, 0, 3));
    settle(*viewer);
    const QPoint scrolled(view->horizontalScrollBar()->value(), view->verticalScrollBar()->value());
    const auto read_before = view->current_pages();
    click(*view, spot_to_page(*view, 0, 3).center());
    settle(*viewer);
    // One row at a time, the row shown holds page 4. Scrolling through every row, the page
    // read is the one at the middle of the window, so only the point says where the view is.
    const auto pages = view->current_pages();
    bool ok = view->continuous() || std::find(pages.begin(), pages.end(), 3) != pages.end();
    double offset = 0.0;
    if (ok && expect_spot) {
      // The link on page 4 whose top left corner is the point named.
      LinkSpot marker;
      for (const auto& spot : link_spots(*view, 3)) {
        if (std::abs(spot.link.area.top() - kMasksSpotY) < 0.5) {
          marker = spot;
        }
      }
      // Turned clockwise once the point is the area's top right corner; twice, its bottom right.
      offset = (turns >= 2 ? marker.rect.bottom() : marker.rect.top()) - kJumpMargin;
      // A row shown alone ends with its page: the view goes as far as it can and the point
      // is on screen, short of the top.
      const auto* vertical = view->verticalScrollBar();
      const bool at_the_end = vertical->maximum() > 0 && vertical->value() == vertical->maximum() && offset > 0.0 &&
                              QRectF(view->viewport()->rect()).contains(marker.rect);
      ok = !marker.rect.isEmpty() && (std::abs(offset) <= 2.0 || (!view->continuous() && at_the_end));
    }
    if (!ok) {
      std::cout << "  " << what << ": page shown " << view->current_page() + 1 << ", the point is " << offset
                << " px from where it belongs\n";
      CHECK(false);
    }
    CHECK(viewer->can_go_back());
    // Back is where the click was made, to the pixel.
    viewer->go_back();
    const QPoint returned(view->horizontalScrollBar()->value(), view->verticalScrollBar()->value());
    if ((returned - scrolled).manhattanLength() > 2 || view->current_pages() != read_before) {
      std::cout << "  " << what << ": Back went to " << returned.x() << ", " << returned.y() << " on page "
                << view->current_page() + 1 << ", the click was made at " << scrolled.x() << ", " << scrolled.y()
                << '\n';
      CHECK(false);
    }
  };

  view->set_continuous(true);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_fit_mode(patchy::PageFitMode::Width);
  follow_masks("continuous, fit width", true);
  view->set_zoom_percent(150.0);
  follow_masks("continuous, 150%", true);
  view->set_fit_mode(patchy::PageFitMode::Width);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPages);
  follow_masks("continuous, two pages", true);
  view->set_reading_direction(patchy::PageReadingDirection::RightToLeft);
  follow_masks("continuous, two pages, right to left", true);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPagesCoverAlone);
  follow_masks("continuous, cover alone, right to left", true);
  view->set_reading_direction(patchy::PageReadingDirection::LeftToRight);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->rotate_clockwise();
  follow_masks("continuous, turned once", true, 1);
  view->rotate_clockwise();
  follow_masks("continuous, turned twice", true, 2);
  view->rotate_clockwise();
  view->rotate_clockwise();
  // One row at a time: the row turns; a page that fits has nowhere to scroll.
  view->set_continuous(false);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  follow_masks("one page at a time", false);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPages);
  follow_masks("two pages at a time", false);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPagesCoverAlone);
  view->set_reading_direction(patchy::PageReadingDirection::RightToLeft);
  follow_masks("cover alone at a time, right to left", false);
  // One row at a time and zoomed in: the row turns and scrolls to the point, or as near as
  // the end of the page lets it.
  view->set_reading_direction(patchy::PageReadingDirection::LeftToRight);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_fit_mode(patchy::PageFitMode::Width);
  follow_masks("one page at a time, fit width", true);
  CHECK(view->verticalScrollBar()->maximum() > 0);
  view->set_zoom_percent(300.0);
  follow_masks("one page at a time, 300%", true);
  // There the page is long enough for the point to reach the top.
  view->first_page();
  settle(*viewer);
  viewer->jump_to(spot_to_page(*view, 0, 3).link.target);
  settle(*viewer);
  CHECK(view->current_page() == 3);
  CHECK(view->verticalScrollBar()->value() < view->verticalScrollBar()->maximum());
  close_viewer(viewer);
}

// Link areas sit on their text at every zoom, in a detail render, at every quarter turn and
// in the two-page layouts, and a click there still follows the link.
void ui_pdf_viewer_links_follow_the_page() {
  ViewerSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_links_areas.pdf"), links_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  view->set_continuous(false);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  expect_links_on_their_text("fit page", *viewer);
  view->set_fit_mode(patchy::PageFitMode::Width);
  expect_links_on_their_text("fit width", *viewer);
  // Zoomed in, with the contents lines brought to the window's corner.
  const auto show_contents = [&] {
    settle(*viewer);
    bring_into_view(*view, spot_to_page(*view, 0, 1));
  };
  view->set_zoom_percent(200.0);
  show_contents();
  expect_links_on_their_text("200%", *viewer);
  // Past what a whole-page image holds: the links are over a detail render.
  view->set_zoom_percent(600.0);
  show_contents();
  expect_links_on_their_text("600%", *viewer);
  hover(*view, spot_to_page(*view, 0, 1).center());
  CHECK(pointing_hand(*view));
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  for (int turn = 1; turn <= 4; ++turn) {
    view->rotate_clockwise();
    expect_links_on_their_text(("turned " + std::to_string(turn % 4) + " times").c_str(), *viewer);
    hover(*view, spot_to_page(*view, 0, 1).center());
    CHECK(pointing_hand(*view));
    hover(*view, QPoint(2, 2));
    CHECK(!pointing_hand(*view));
  }
  view->set_spread_mode(patchy::PageSpreadMode::TwoPages);
  expect_links_on_their_text("two pages", *viewer);
  view->set_reading_direction(patchy::PageReadingDirection::RightToLeft);
  expect_links_on_their_text("two pages, right to left", *viewer);
  CHECK(view->current_pages() == std::vector<int>({0, 1}));
  // Page 2 is on the left now: its link back goes to page 1, page 1's first goes to page 2.
  const auto back = spot_to_page(*view, 1, 0);
  const auto forward = spot_to_page(*view, 0, 1);
  CHECK(back.rect.center().x() < forward.rect.center().x());
  view->set_spread_mode(patchy::PageSpreadMode::TwoPagesCoverAlone);
  expect_links_on_their_text("cover alone, right to left", *viewer);
  click(*view, spot_to_page(*view, 0, 2).center());
  settle(*viewer);
  CHECK(view->current_pages() == std::vector<int>({1, 2}));
  expect_links_on_their_text("pages 2 and 3, right to left", *viewer);
  // The hand follows a scroll under a still pointer.
  view->set_reading_direction(patchy::PageReadingDirection::LeftToRight);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_continuous(true);
  view->set_fit_mode(patchy::PageFitMode::Width);
  view->first_page();
  settle(*viewer);
  const auto line = spot_to_page(*view, 0, 1);
  const int spacing = spot_to_page(*view, 0, 2).center().y() - line.center().y();
  CHECK(spacing > 8);
  hover(*view, line.center());
  CHECK(pointing_hand(*view));
  // Half a line down the pointer is between two links; a whole line down, on the next.
  const int start = view->verticalScrollBar()->value();
  view->verticalScrollBar()->setValue(start + spacing / 2);
  QApplication::processEvents();
  CHECK(!pointing_hand(*view));
  view->verticalScrollBar()->setValue(start + spacing);
  QApplication::processEvents();
  CHECK(pointing_hand(*view));
  const auto* under = view->link_at(QPointF(line.center()));
  CHECK(under != nullptr && under->target.page == 2);
  // Off the pages the hand goes.
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(view->viewport(), &leave);
  CHECK(!pointing_hand(*view));
  close_viewer(viewer);
}

// Right-click on a link: Open Link (web and mail addresses only) and Copy Link Address, or
// Go to Page for a link into the document. No menu anywhere else.
void ui_pdf_viewer_links_context_menu() {
  ViewerSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_links_menu.pdf"), links_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  LinkLog log;
  auto* viewer = open_viewer(window, path, &log);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  view->set_continuous(false);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  settle(*viewer);
  const auto web = spot_with_address(*view, 0, kWebAddress);
  const auto mail = spot_with_address(*view, 0, QStringLiteral("mailto:"));
  const auto file = spot_with_address(*view, 0, kFileAddress);
  const auto script = spot_with_address(*view, 0, QStringLiteral("javascript:"));
  const auto program = spot_with_address(*view, 0, QStringLiteral("file:calc"));
  const QString open_name = QStringLiteral("pdfViewerLinkOpenAction");
  const QString copy_name = QStringLiteral("pdfViewerLinkCopyAction");
  // Under the link's own entries, a separator and the entries every page has: Copy (off
  // with no text selected) and Select All on This Page.
  const QString copy_text_name = QStringLiteral("pdfViewerCopyAction");
  const QString select_page_name = QStringLiteral("pdfViewerSelectPageAction");

  auto menu = right_click(*view, web.center());
  CHECK(menu.shown);
  CHECK(menu.actions == QStringList({open_name, copy_name, QString(), copy_text_name, select_page_name}));
  CHECK(menu.texts == QStringList({QStringLiteral("Open Link"), QStringLiteral("Copy Link Address"), QString(),
                                   QStringLiteral("Copy"), QStringLiteral("Select All on This Page")}));
  CHECK(menu.action_enabled(open_name) && menu.action_enabled(copy_name));
  CHECK(!menu.action_enabled(copy_text_name) && menu.action_enabled(select_page_name));
  CHECK(log.opened.isEmpty() && log.copied.isEmpty());
  right_click(*view, web.center(), copy_name);
  CHECK(log.copied == QStringList({kWebAddress}));
  right_click(*view, web.center(), open_name);
  CHECK(log.opened == QList<QUrl>({QUrl(kWebAddress)}));
  menu = right_click(*view, mail.center(), open_name);
  CHECK(menu.action_enabled(open_name));
  CHECK(log.opened.size() == 2 && log.opened.value(1) == QUrl(kMailAddress));
  // What does not open can still be copied.
  for (const auto* spot : {&file, &script, &program}) {
    menu = right_click(*view, spot->center(), open_name);
    CHECK(menu.shown && !menu.action_enabled(open_name) && menu.action_enabled(copy_name));
  }
  CHECK(log.opened.size() == 2);
  right_click(*view, file.center(), copy_name);
  CHECK(log.copied.size() == 2 && log.copied.value(1) == kFileAddress);
  // The viewer hands nothing else to the opener even when asked directly.
  emit view->url_open_requested(QUrl(kFileAddress));
  emit view->url_open_requested(QUrl(QStringLiteral("javascript:alert(1)")));
  CHECK(log.opened.size() == 2);

  // A link into the document.
  menu = right_click(*view, spot_to_page(*view, 0, 4).center());
  CHECK(menu.actions ==
        QStringList({QStringLiteral("pdfViewerLinkGoAction"), QString(), copy_text_name, select_page_name}));
  CHECK(menu.texts.value(0) == QStringLiteral("Go to Page 5"));
  CHECK(view->current_page() == 0);
  right_click(*view, spot_to_page(*view, 0, 4).center(), QStringLiteral("pdfViewerLinkGoAction"));
  CHECK(view->current_page() == 4 && viewer->can_go_back());
  viewer->go_back();
  // Off a link the page has its own two entries, and beside the page there is no menu.
  menu = right_click(*view, QPoint(view->viewport()->width() / 2, view->viewport()->height() - 40));
  CHECK(menu.shown && menu.actions == QStringList({copy_text_name, select_page_name}));
  CHECK(!menu.action_enabled(copy_text_name));
  menu = right_click(*view, QPoint(3, view->viewport()->height() / 2));
  CHECK(!menu.shown);
  close_viewer(viewer);
}

// --- Back and Forward ---

// Back and Forward as typed and clicked: Alt+Left and Alt+Right through the window system's
// key path, the mouse's side buttons, the toolbar buttons. Jumps fill the history; page
// turns by keys and the wheel do not.
void ui_pdf_viewer_history_back_and_forward() {
  ViewerSettingsGuard settings_guard;
  SettingsValueRestorer viewer_setting(QStringLiteral("imports/pdfOpenInViewer"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfOpenInViewer"), true);
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_history.pdf"), links_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  // The main window in use first, so its hotkeys and event filters are live.
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
  auto* back = viewer->findChild<QAction*>(QStringLiteral("pdfViewerBackAction"));
  auto* forward = viewer->findChild<QAction*>(QStringLiteral("pdfViewerForwardAction"));
  auto* page_spin = viewer->findChild<QSpinBox*>(QStringLiteral("pdfViewerPageSpin"));
  auto* thumbnails = viewer->findChild<QListWidget*>(QStringLiteral("pdfViewerThumbnails"));
  CHECK(handle != nullptr && back != nullptr && forward != nullptr && page_spin != nullptr && thumbnails != nullptr);
  if (handle == nullptr || back == nullptr || forward == nullptr || page_spin == nullptr || thumbnails == nullptr) {
    return;
  }
  const auto type = [handle](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::keyClick(handle, key, modifiers);
  };
  const auto expect = [&](const char* what, int page, bool can_back, bool can_forward) {
    if (view->current_page() != page || viewer->can_go_back() != can_back || viewer->can_go_forward() != can_forward ||
        back->isEnabled() != can_back || forward->isEnabled() != can_forward) {
      std::cout << "  " << what << ": page " << view->current_page() + 1 << " (wanted " << page + 1 << "), back "
                << viewer->can_go_back() << "/" << back->isEnabled() << " (wanted " << can_back << "), forward "
                << viewer->can_go_forward() << "/" << forward->isEnabled() << " (wanted " << can_forward << ")\n";
      CHECK(false);
    }
  };
  view->set_continuous(false);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->first_page();
  view->setFocus();
  settle(*viewer);
  // The tips name the keys.
  CHECK(back->toolTip().contains(QKeySequence(Qt::ALT | Qt::Key_Left).toString(QKeySequence::NativeText)));
  CHECK(forward->toolTip().contains(QKeySequence(Qt::ALT | Qt::Key_Right).toString(QKeySequence::NativeText)));
  CHECK(!back->icon().isNull() && !forward->icon().isNull());
  expect("at the start", 0, false, false);

  // Page turns are not jumps: keys, the wheel, the toolbar's page buttons.
  type(Qt::Key_PageDown);
  type(Qt::Key_Space);
  type(Qt::Key_Right);
  expect("after three page turns", 3, false, false);
  send_wheel(view->viewport(), -120);
  QApplication::processEvents();
  type(Qt::Key_Home);
  expect("after Home", 0, false, false);
  // With nowhere to go back to, Alt+Left does nothing: it does not turn the page (as the
  // arrow alone would) and it does not reach the main window.
  type(Qt::Key_Right);
  type(Qt::Key_Left, Qt::AltModifier);
  type(Qt::Key_Right, Qt::AltModifier);
  expect("Alt+arrows with no history", 1, false, false);
  type(Qt::Key_Home);

  // The page field is a jump.
  page_spin->setFocus(Qt::MouseFocusReason);
  page_spin->selectAll();
  type(Qt::Key_5);
  type(Qt::Key_Return);
  expect("page 5 typed", 4, true, false);
  // A link is a jump.
  settle(*viewer);
  click(*view, spot_to_page(*view, 4, 0).center());
  expect("Back to contents clicked", 0, true, false);
  // Alt+Left and Alt+Right, with the keyboard on the pages.
  view->setFocus(Qt::MouseFocusReason);
  type(Qt::Key_Left, Qt::AltModifier);
  expect("Alt+Left", 4, true, true);
  type(Qt::Key_Left, Qt::AltModifier);
  expect("Alt+Left again", 0, false, true);
  type(Qt::Key_Left, Qt::AltModifier);
  expect("Alt+Left at the start", 0, false, true);
  type(Qt::Key_Right, Qt::AltModifier);
  expect("Alt+Right", 4, true, true);
  type(Qt::Key_Right, Qt::AltModifier);
  expect("Alt+Right again", 0, true, false);
#if !defined(Q_OS_MACOS)
  // And with the keyboard in the page field or on the page list (Option and an arrow move
  // by a word in a field on macOS, so there the field keeps them).
  page_spin->setFocus(Qt::MouseFocusReason);
  type(Qt::Key_Left, Qt::AltModifier);
  expect("Alt+Left in the page field", 4, true, true);
  thumbnails->setFocus(Qt::MouseFocusReason);
  type(Qt::Key_Right, Qt::AltModifier);
  expect("Alt+Right on the page list", 0, true, false);
#endif
  // The mouse's side buttons, over the pages and over the sidebar.
  const QPoint middle = view->viewport()->rect().center();
  send_mouse(*view->viewport(), QEvent::MouseButtonPress, middle, Qt::BackButton, Qt::BackButton);
  send_mouse(*view->viewport(), QEvent::MouseButtonRelease, middle, Qt::BackButton, Qt::NoButton);
  expect("the mouse's Back button", 4, true, true);
  send_mouse(*thumbnails->viewport(), QEvent::MouseButtonPress, QPoint(10, 10), Qt::ForwardButton, Qt::ForwardButton);
  send_mouse(*thumbnails->viewport(), QEvent::MouseButtonRelease, QPoint(10, 10), Qt::ForwardButton, Qt::NoButton);
  expect("the mouse's Forward button over the sidebar", 0, true, false);
  // The toolbar buttons.
  back->trigger();
  expect("the Back button", 4, true, true);
  forward->trigger();
  expect("the Forward button", 0, true, false);
  // A new jump drops what was ahead.
  back->trigger();
  back->trigger();
  expect("back to the start", 0, false, true);
  QApplication::processEvents();
  auto* third = thumbnails->item(2);
  CHECK(third != nullptr);
  if (third != nullptr) {
    const QPoint center = thumbnails->visualItemRect(third).center();
    send_mouse(*thumbnails->viewport(), QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*thumbnails->viewport(), QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    expect("a page clicked in the page list", 2, true, false);
  }
  // None of this reached the main window.
  CHECK(MainWindowTestAccess::session_count(window) == sessions);

  // Back returns to the place left, not only its page: the scroll position, and the same
  // part of the page at another zoom.
  // (Zoomed until the page is wider than the window, so there is a place across as well.)
  view->set_continuous(true);
  view->set_zoom_percent(300.0);
  settle(*viewer);
  CHECK(view->horizontalScrollBar()->maximum() > 400);
  view->verticalScrollBar()->setValue(4100);
  view->horizontalScrollBar()->setValue(300);
  QApplication::processEvents();
  const int scrolled_down = view->verticalScrollBar()->value();
  const int scrolled_across = view->horizontalScrollBar()->value();
  const auto left = view->view_position();
  CHECK(left.anchor_page == 1 && left.fx > 0.05 && left.fy > 0.05);
  viewer->jump_to({5, QPointF()});
  CHECK(view->current_page() == 5);
  viewer->go_back();
  CHECK(std::abs(view->verticalScrollBar()->value() - scrolled_down) <= 1);
  CHECK(std::abs(view->horizontalScrollBar()->value() - scrolled_across) <= 1);
  viewer->go_forward();
  CHECK(view->current_page() == 5);
  view->set_zoom_percent(400.0);
  viewer->go_back();
  const auto returned = view->view_position();
  if (returned.anchor_page != left.anchor_page || std::abs(returned.fx - left.fx) > 0.01 ||
      std::abs(returned.fy - left.fy) > 0.01) {
    std::cout << "  at another zoom Back went to page " << returned.anchor_page + 1 << " at " << returned.fx << ", "
              << returned.fy << "; the place left was page " << left.anchor_page + 1 << " at " << left.fx << ", "
              << left.fy << '\n';
    CHECK(false);
  }
  const QPointer<PdfViewerWindow> guard(viewer);
  viewer->close();
  CHECK(process_events_until([&guard] { return guard.isNull(); }));
}

// --- Outline ---

QTreeWidgetItem* outline_item(QTreeWidget& outline, const QString& title) {
  const auto found = outline.findItems(title, Qt::MatchExactly | Qt::MatchRecursive);
  if (found.size() != 1) {
    std::cout << "  outline entries named \"" << title.toStdString() << "\": " << found.size() << '\n';
    CHECK(false);
    return nullptr;
  }
  return found.front();
}

void click_outline(QTreeWidget& outline, QTreeWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  outline.scrollToItem(item);
  const QRect rect = outline.visualItemRect(item);
  // Past the branch arrow, on the title.
  const QPoint position(rect.left() + std::min(24, rect.width() / 2), rect.center().y());
  send_mouse(*outline.viewport(), QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*outline.viewport(), QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
}

QString current_title(const QTreeWidget& outline) {
  return outline.currentItem() != nullptr ? outline.currentItem()->text(0) : QString();
}

// The Outline view of the sidebar: the PDF's bookmarks as a tree, a click jumps, the entry
// being read is marked, and the view shown is remembered.
void ui_pdf_viewer_outline_sidebar() {
  ViewerSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_outline.pdf"), links_pdf_bytes());
  const QString plain = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_outline_none.pdf"), plain_pdf_bytes());
  const QString lone =
      write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_outline_lone.pdf"), links_pdf_bytes(true));
  patchy::ui::MainWindow window;
  show_window(window);
  struct Sidebar {
    QTabBar* tabs{nullptr};
    QStackedWidget* stack{nullptr};
    QTreeWidget* outline{nullptr};
    QListWidget* thumbnails{nullptr};
    [[nodiscard]] bool complete() const {
      return tabs != nullptr && stack != nullptr && outline != nullptr && thumbnails != nullptr;
    }
  };
  const auto sidebar_of = [](PdfViewerWindow& viewer) {
    Sidebar sidebar;
    sidebar.tabs = viewer.findChild<QTabBar*>(QStringLiteral("pdfViewerSidebarTabs"));
    sidebar.stack = viewer.findChild<QStackedWidget*>(QStringLiteral("pdfViewerSidebarStack"));
    sidebar.outline = viewer.findChild<QTreeWidget*>(QStringLiteral("pdfViewerOutline"));
    sidebar.thumbnails = viewer.findChild<QListWidget*>(QStringLiteral("pdfViewerThumbnails"));
    CHECK(sidebar.complete());
    return sidebar;
  };

  auto* viewer = open_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  Sidebar sidebar = sidebar_of(*viewer);
  if (!sidebar.complete()) {
    return;
  }
  view->set_continuous(true);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_fit_mode(patchy::PageFitMode::Width);
  settle(*viewer);
  // A PDF with an outline has the two tabs; the pages show first.
  CHECK(sidebar.tabs->isVisible() && sidebar.tabs->count() == 2);
  CHECK(sidebar.tabs->tabText(0) == QStringLiteral("Pages") && sidebar.tabs->tabText(1) == QStringLiteral("Outline"));
  CHECK(sidebar.tabs->currentIndex() == 0 && sidebar.stack->currentWidget() == sidebar.thumbnails);
  const int narrow = sidebar.stack->width();
  sidebar.tabs->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(sidebar.stack->currentWidget() == sidebar.outline && sidebar.outline->isVisible());
  // Titles get more room than thumbnails.
  CHECK(sidebar.stack->width() > narrow);
  // The tree as the PDF has it: three entries at the top, closed.
  auto* outline = sidebar.outline;
  CHECK(outline->topLevelItemCount() == 3);
  auto* guide = outline_item(*outline, QStringLiteral("A Short Guide to Layers"));
  auto* masks = outline_item(*outline, QStringLiteral("3. Masks"));
  auto* painting = outline_item(*outline, QStringLiteral("Painting on a mask"));
  auto* groups = outline_item(*outline, QStringLiteral("4. Groups"));
  auto* saving = outline_item(*outline, QStringLiteral("5. Saving your work"));
  auto* website = outline_item(*outline, QStringLiteral("Patchy on GitHub"));
  if (guide == nullptr || masks == nullptr || painting == nullptr || groups == nullptr || saving == nullptr ||
      website == nullptr) {
    close_viewer(viewer);
    return;
  }
  CHECK(guide->childCount() == 4 && masks->parent() == guide && painting->parent() == masks);
  CHECK(saving->parent() == nullptr && website->parent() == nullptr);
  CHECK(!guide->isExpanded() && !masks->isExpanded());
  CHECK(saving->toolTip(0) == QStringLiteral("Page 6") && website->toolTip(0).isEmpty());
  // The entry being read is marked: on page 1 that is the guide itself.
  CHECK(current_title(*outline) == QStringLiteral("A Short Guide to Layers"));

  // A click jumps and hands the keyboard to the pages; Back returns.
  click_outline(*outline, saving);
  CHECK(view->current_page() == 5);
  CHECK(viewer->can_go_back());
  CHECK(current_title(*outline) == QStringLiteral("5. Saving your work"));
  CHECK(viewer->focusWidget() == view);
  viewer->go_back();
  CHECK(view->current_page() == 0);
  // An entry that goes nowhere in the document does nothing.
  click_outline(*outline, website);
  CHECK(view->current_page() == 0 && !viewer->can_go_back());

  // Nested entries. An entry with a point lands on it: the link on page 4 that starts at
  // that point is at the top of the window.
  guide->setExpanded(true);
  masks->setExpanded(true);
  QApplication::processEvents();
  click_outline(*outline, painting);
  settle(*viewer);
  LinkSpot marker;
  for (const auto& spot : link_spots(*view, 3)) {
    if (std::abs(spot.link.area.top() - kMasksSpotY) < 0.5) {
      marker = spot;
    }
  }
  CHECK(!marker.rect.isEmpty() && std::abs(marker.rect.top() - kJumpMargin) <= 2.0);
  // Two entries start on page 4: the one clicked stays marked.
  CHECK(current_title(*outline) == QStringLiteral("Painting on a mask"));
  click_outline(*outline, groups);
  CHECK(view->current_page() == 4 && current_title(*outline) == QStringLiteral("4. Groups"));

  // The mark follows the reading: page turns move it, to the row that shows the entry.
  view->go_to_page(2);
  CHECK(current_title(*outline) == QStringLiteral("2. Opacity and blending"));
  view->go_to_page(3);
  CHECK(current_title(*outline) == QStringLiteral("3. Masks"));
  masks->setExpanded(false);
  view->go_to_page(4);
  view->go_to_page(3);
  CHECK(current_title(*outline) == QStringLiteral("3. Masks"));
  guide->setExpanded(false);
  QApplication::processEvents();
  CHECK(current_title(*outline) == QStringLiteral("A Short Guide to Layers"));
  view->go_to_page(5);
  CHECK(current_title(*outline) == QStringLiteral("5. Saving your work"));
  // Two pages in a row: an entry that starts on the second page counts.
  guide->setExpanded(true);
  view->set_continuous(false);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPages);
  view->go_to_page(0);
  CHECK(view->current_pages() == std::vector<int>({0, 1}));
  CHECK(current_title(*outline) == QStringLiteral("1. What a layer is"));
  close_viewer(viewer);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("pdfViewer/sidebarView")).toString() ==
        QStringLiteral("outline"));

  // The view shown is remembered for the next PDF with an outline.
  viewer = open_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  sidebar = sidebar_of(*viewer);
  if (sidebar.complete()) {
    CHECK(sidebar.tabs->isVisible() && sidebar.tabs->currentIndex() == 1);
    CHECK(sidebar.stack->currentWidget() == sidebar.outline);
  }
  close_viewer(viewer);
  // A PDF without an outline shows its pages and no tabs, and changes nothing remembered.
  viewer = open_viewer(window, plain);
  if (viewer == nullptr) {
    return;
  }
  sidebar = sidebar_of(*viewer);
  if (sidebar.complete()) {
    QApplication::processEvents();
    CHECK(!sidebar.tabs->isVisible());
    CHECK(sidebar.stack->currentWidget() == sidebar.thumbnails && sidebar.thumbnails->count() == 2);
    CHECK(sidebar.outline->topLevelItemCount() == 0);
  }
  close_viewer(viewer);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("pdfViewer/sidebarView")).toString() ==
        QStringLiteral("outline"));
  // A single entry at the top is opened; choosing Pages is remembered too.
  viewer = open_viewer(window, lone);
  if (viewer == nullptr) {
    return;
  }
  sidebar = sidebar_of(*viewer);
  if (sidebar.complete()) {
    CHECK(sidebar.tabs->currentIndex() == 1);
    CHECK(sidebar.outline->topLevelItemCount() == 1 && sidebar.outline->topLevelItem(0)->isExpanded());
    sidebar.tabs->setCurrentIndex(0);
    QApplication::processEvents();
    CHECK(sidebar.stack->currentWidget() == sidebar.thumbnails);
  }
  close_viewer(viewer);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("pdfViewer/sidebarView")).toString() ==
        QStringLiteral("pages"));
}

// The viewer on the guide with its outline open and the pointer over a link: the picture
// for the feature's card (the tooltip is its own window, painted in where it shows).
void ui_pdf_viewer_links_sample_document() {
  ViewerSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_links_sample.pdf"), links_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_viewer(window, path, nullptr, QSize(1240, 820));
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  auto* tabs = viewer->findChild<QTabBar*>(QStringLiteral("pdfViewerSidebarTabs"));
  auto* outline = viewer->findChild<QTreeWidget*>(QStringLiteral("pdfViewerOutline"));
  CHECK(tabs != nullptr && outline != nullptr);
  if (tabs == nullptr || outline == nullptr) {
    return;
  }
  view->set_continuous(false);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_fit_mode(patchy::PageFitMode::Width);
  tabs->setCurrentIndex(1);
  outline->expandAll();
  // Jumps first, so Back and Forward are lit on the contents page.
  view->go_to_page(1);
  viewer->jump_to({0, QPointF()});
  viewer->jump_to({5, QPointF()});
  viewer->go_back();
  (void)process_events_until([] { return false; }, 200);
  settle(*viewer);
  CHECK(viewer->can_go_back() && viewer->can_go_forward());
  CHECK(view->current_page() == 0);
  const auto web = spot_with_address(*view, 0, kWebAddress);
  const QPoint pointer = web.center();
  hover(*view, pointer);
  CHECK(pointing_hand(*view));
  CHECK(tip_at(*view, pointer).startsWith(kWebAddress));
  QImage shot = patchy::test::ui::capture_widget_artifact("ui_pdf_viewer_links_window", *viewer);
  CHECK(!shot.isNull());
  for (auto* top : QApplication::topLevelWidgets()) {
    if (!shot.isNull() && top->inherits("QTipLabel") && top->isVisible()) {
      const double scale = shot.width() / std::max(1.0, static_cast<double>(viewer->width()));
      const QImage tip = top->grab().toImage();
      QPainter painter(&shot);
      const QPoint place = top->mapToGlobal(QPoint(0, 0)) - viewer->mapToGlobal(QPoint(0, 0));
      painter.drawImage(QPointF(place.x() * scale, place.y() * scale), tip);
    }
  }
  if (!shot.isNull()) {
    CHECK(shot.save(QStringLiteral("test-artifacts/ui_pdf_viewer_links.png")));
  }
  QToolTip::hideText();
  close_viewer(viewer);
}

}  // namespace

std::vector<patchy::test::TestCase> pdf_viewer_links_ui_tests() {
  return {
      {"ui_pdf_viewer_links_are_read_from_the_pdf", ui_pdf_viewer_links_are_read_from_the_pdf},
      {"ui_pdf_viewer_links_hover_and_click", ui_pdf_viewer_links_hover_and_click},
      {"ui_pdf_viewer_links_jump_in_every_layout", ui_pdf_viewer_links_jump_in_every_layout},
      {"ui_pdf_viewer_links_follow_the_page", ui_pdf_viewer_links_follow_the_page},
      {"ui_pdf_viewer_links_context_menu", ui_pdf_viewer_links_context_menu},
      {"ui_pdf_viewer_history_back_and_forward", ui_pdf_viewer_history_back_and_forward},
      {"ui_pdf_viewer_outline_sidebar", ui_pdf_viewer_outline_sidebar},
      {"ui_pdf_viewer_links_sample_document", ui_pdf_viewer_links_sample_document},
  };
}

#else

std::vector<patchy::test::TestCase> pdf_viewer_links_ui_tests() {
  return {};
}

#endif
