#include "ui/pdf_book_print_dialog.hpp"

#include "formats/pdf_image_writer.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/pdf_export.hpp"
#include "ui/qt_paths.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPixmap>
#include <QPrintDialog>
#include <QPrinter>
#include <QProgressDialog>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QPalette>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <memory>
#include <stdexcept>

namespace patchy::ui {

namespace {

constexpr double kPointsPerMm = 72.0 / 25.4;
constexpr double kPointsPerInch = 72.0;
constexpr std::array<QPageSize::PageSizeId, 7> kPapers{QPageSize::Letter, QPageSize::Legal, QPageSize::Tabloid,
                                                       QPageSize::A4,     QPageSize::A3,    QPageSize::A5,
                                                       QPageSize::B5};
constexpr std::array<int, 3> kResolutions{150, 300, 600};

// Permanent settings keys and tokens (docs/pdf-book-print.md).
QString settings_key(const char* leaf) {
  return QStringLiteral("pdfBookPrint/") + QLatin1String(leaf);
}

QString style_token(BookPrintStyle style) {
  switch (style) {
    case BookPrintStyle::Signatures:
      return QStringLiteral("signatures");
    case BookPrintStyle::CutAndStack:
      return QStringLiteral("cut-stack");
    case BookPrintStyle::MiniZine:
      return QStringLiteral("zine");
    case BookPrintStyle::Booklet:
      break;
  }
  return QStringLiteral("booklet");
}

BookPrintStyle style_from_token(const QString& token) {
  if (token == QStringLiteral("signatures")) {
    return BookPrintStyle::Signatures;
  }
  if (token == QStringLiteral("cut-stack")) {
    return BookPrintStyle::CutAndStack;
  }
  if (token == QStringLiteral("zine")) {
    return BookPrintStyle::MiniZine;
  }
  return BookPrintStyle::Booklet;
}

QString page_numbers_token(BookPageNumbers numbers) {
  switch (numbers) {
    case BookPageNumbers::All:
      return QStringLiteral("all");
    case BookPageNumbers::SkipCovers:
      return QStringLiteral("inside");
    case BookPageNumbers::None:
      break;
  }
  return QStringLiteral("none");
}

BookPageNumbers page_numbers_from_token(const QString& token) {
  if (token == QStringLiteral("all")) {
    return BookPageNumbers::All;
  }
  if (token == QStringLiteral("inside")) {
    return BookPageNumbers::SkipCovers;
  }
  return BookPageNumbers::None;
}

bool nested_style(BookPrintStyle style) {
  return style == BookPrintStyle::Booklet || style == BookPrintStyle::Signatures;
}

QImage turned_half(const QImage& image) {
  return image.transformed(QTransform().rotate(180.0));
}

void draw_marks(QPainter& painter, const PdfBookPrintOptions& options, const BookImposition& imposition,
                const BookSheet& sheet, QSizeF size, bool back) {
  if (!options.marks || back) {
    return;
  }
  const double w = size.width();
  const double h = size.height();
  // Ticks stay inside the outer margin, so they never print over a page.
  const double tick = std::min(options.outer_margin_mm * kPointsPerMm * 0.8, 0.25 * kPointsPerInch);
  QPen dashed(QColor(120, 120, 120));  // color-literal-ok: ink of a printed fold mark
  dashed.setWidthF(0.5);
  dashed.setStyle(Qt::DashLine);
  QPen solid(QColor(0, 0, 0));  // color-literal-ok: ink of a printed cut mark
  solid.setWidthF(0.5);
  const auto edge_ticks = [&](double x) {
    if (tick >= 2.0) {
      painter.drawLine(QPointF(x, 0.0), QPointF(x, tick));
      painter.drawLine(QPointF(x, h - tick), QPointF(x, h));
    }
  };
  switch (options.style) {
    case BookPrintStyle::Booklet:
    case BookPrintStyle::Signatures:
      painter.setPen(dashed);
      edge_ticks(w / 2.0);
      if (options.style == BookPrintStyle::Signatures && sheet.sheet_in_signature == 0 && sheet.signature > 0) {
        // Collation mark: a block straddling the fold of each signature's outer sheet, one
        // step lower per signature, so the folded stack shows a staircase down the spine.
        const double mark_w = 3.0 * kPointsPerMm;
        const double mark_h = 6.0 * kPointsPerMm;
        const double top = options.outer_margin_mm * kPointsPerMm;
        const double room = std::max(mark_h, h - 2.0 * top - mark_h);
        const double y = top + std::fmod((sheet.signature - 1) * mark_h, room);
        painter.fillRect(QRectF(w / 2.0 - mark_w / 2.0, y, mark_w, mark_h),
                         QColor(0, 0, 0));  // color-literal-ok: printed collation mark
      }
      break;
    case BookPrintStyle::CutAndStack:
      painter.setPen(solid);
      edge_ticks(w / 2.0);
      break;
    case BookPrintStyle::MiniZine: {
      painter.setPen(dashed);
      for (int column = 1; column < imposition.columns; ++column) {
        edge_ticks(w * column / imposition.columns);
      }
      if (tick >= 2.0) {
        painter.drawLine(QPointF(0.0, h / 2.0), QPointF(tick, h / 2.0));
        painter.drawLine(QPointF(w - tick, h / 2.0), QPointF(w, h / 2.0));
      }
      painter.setPen(solid);
      painter.drawLine(QPointF(w / 4.0, h / 2.0), QPointF(3.0 * w / 4.0, h / 2.0));
      break;
    }
  }
}

// The page's number in the book, centred under the page in the space below it, or, when the
// page reaches the bottom of its cell, over the page's foot on a small white label.
void draw_page_number(QPainter& painter, const BookPagePlacement& placed, const BookRect& cell, int number) {
  QFont font = QApplication::font();
  font.setPointSizeF(8.0);
  painter.setFont(font);
  const QString text = QString::number(number);
  const double text_height = QFontMetricsF(font).height();
  const double page_bottom = std::min(placed.page.y + placed.page.height, placed.clip.y + placed.clip.height);
  const double below = cell.y + cell.height - page_bottom;
  const double x = placed.clip.x;
  const double width = placed.clip.width;
  painter.setPen(QColor(0, 0, 0));  // color-literal-ok: printed page number
  if (below >= text_height + 2.0) {
    painter.drawText(QRectF(x, page_bottom, width, std::min(below, 2.0 * text_height)), Qt::AlignCenter, text);
    return;
  }
  const double label_width = QFontMetricsF(font).horizontalAdvance(text) + 6.0;
  const QRectF label(x + (width - label_width) / 2.0, page_bottom - text_height - 3.0, label_width, text_height);
  painter.fillRect(label, QColor(255, 255, 255));  // color-literal-ok: paper behind a printed page number
  painter.drawText(label, Qt::AlignCenter, text);
}

QString page_name(const BookImposition& imposition, const std::vector<BookSourcePage>& source_pages,
                  int book_page) {
  if (book_page < 0 || book_page >= static_cast<int>(imposition.book_pages.size())) {
    return QObject::tr("a blank page");
  }
  const int index = imposition.book_pages[static_cast<std::size_t>(book_page)];
  if (index < 0 || index >= static_cast<int>(source_pages.size())) {
    return QObject::tr("a blank page");
  }
  const BookSourcePage& source = source_pages[static_cast<std::size_t>(index)];
  switch (source.part) {
    case BookPagePart::Left:
      return QObject::tr("the left half of page %1").arg(source.page + 1);
    case BookPagePart::Right:
      return QObject::tr("the right half of page %1").arg(source.page + 1);
    case BookPagePart::Whole:
      break;
  }
  return QObject::tr("page %1").arg(source.page + 1);
}

int slot_page(const std::vector<BookSheetSlot>& side, int column) {
  for (const auto& slot : side) {
    if (slot.column == column && slot.row == 0) {
      return slot.book_page;
    }
  }
  return -1;
}

// The steps as a page of their own, laid out the way the dialog shows them. The image's
// resolution is set to `dpi`, so point sizes come out at their real size on paper.
QImage render_steps_page(const PdfBookPrintOptions& options, const BookAssemblyGuide& guide, double dpi) {
  const QSizeF sheet = book_sheet_points(options);
  QImage image(QSize(std::max(1, static_cast<int>(std::lround(sheet.width() / 72.0 * dpi))),
                     std::max(1, static_cast<int>(std::lround(sheet.height() / 72.0 * dpi)))),
               QImage::Format_RGB32);
  const int dots_per_meter = static_cast<int>(std::lround(dpi / 0.0254));
  image.setDotsPerMeterX(dots_per_meter);
  image.setDotsPerMeterY(dots_per_meter);
  image.fill(Qt::white);  // color-literal-ok: the paper
  QPainter painter(&image);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const double margin = 0.5 * dpi;
  QTextDocument text;
  text.documentLayout()->setPaintDevice(&image);
  QFont font = QApplication::font();
  font.setPointSizeF(10.0);
  text.setDefaultFont(font);
  text.setHtml(QStringLiteral("<h3>") + QObject::tr("Putting the book together").toHtmlEscaped() +
               QStringLiteral("</h3>") + book_assembly_html(guide));
  text.setTextWidth(image.width() - 2.0 * margin);
  painter.translate(margin, margin);
  QAbstractTextDocumentLayout::PaintContext context;
  context.palette.setColor(QPalette::Text, QColor(0, 0, 0));  // color-literal-ok: printed text
  context.clip = QRectF(0.0, 0.0, image.width() - 2.0 * margin, image.height() - 2.0 * margin);
  text.documentLayout()->draw(&painter, context);
  painter.end();
  return image;
}

// Runs `work` on a worker and keeps the window responsive until it finishes.
template <typename Result>
Result wait_with_events(std::future<Result> future) {
  while (future.wait_for(std::chrono::milliseconds(30)) != std::future_status::ready) {
    QApplication::processEvents(QEventLoop::AllEvents, 30);
  }
  return future.get();
}

struct SideRef {
  int sheet{0};
  bool back{false};
};

std::vector<SideRef> printed_sides(const BookImposition& imposition, bool fronts, bool backs) {
  std::vector<SideRef> sides;
  for (int sheet = 0; sheet < static_cast<int>(imposition.sheets.size()); ++sheet) {
    if (fronts) {
      sides.push_back({sheet, false});
    }
    if (backs && imposition.two_sided) {
      sides.push_back({sheet, true});
    }
  }
  return sides;
}

}  // namespace

std::vector<BookPageSize> book_pdf_page_sizes(QPdfDocument& document) {
  std::vector<BookPageSize> sizes;
  const int count = std::max(0, document.pageCount());
  sizes.reserve(static_cast<std::size_t>(count));
  for (int page = 0; page < count; ++page) {
    const QSizeF points = document.pagePointSize(page);
    sizes.push_back({points.width(), points.height()});
  }
  return sizes;
}

std::vector<BookSourcePage> book_source_pages(const PdfBookPrintOptions& options,
                                              const std::vector<BookPageSize>& page_sizes) {
  return book_source_pages(page_sizes, options.first_page, options.last_page, options.spreads,
                           options.right_to_left);
}

BookImposition book_imposition_for(const PdfBookPrintOptions& options,
                                   const std::vector<BookSourcePage>& source_pages) {
  BookImpositionOptions imposition;
  imposition.style = options.style;
  imposition.page_count = static_cast<int>(source_pages.size());
  imposition.sheets_per_signature = options.sheets_per_signature;
  imposition.right_to_left = options.right_to_left;
  // A cover spread's other half is the back cover: blanks go before it.
  const bool cover_spread = options.spreads == BookSpreads::SplitCoverSpread && !source_pages.empty() &&
                            source_pages.back().part != BookPagePart::Whole &&
                            source_pages.back().page == source_pages.front().page;
  imposition.last_page_is_back_cover = options.last_page_is_back_cover || cover_spread;
  return impose_book(imposition);
}

QSizeF book_sheet_points(const PdfBookPrintOptions& options) {
  const QSizeF size = QPageSize(options.paper).size(QPageSize::Point);
  return {std::max(size.width(), size.height()), std::min(size.width(), size.height())};
}

QImage render_book_sheet_side(QPdfDocument& document, const PdfBookPrintOptions& options,
                              const BookImposition& imposition, const std::vector<BookSourcePage>& source_pages,
                              int sheet, bool back, double dpi) {
  const QSizeF size = book_sheet_points(options);
  const double scale = dpi / 72.0;
  QImage image(QSize(std::max(1, static_cast<int>(std::lround(size.width() * scale))),
                     std::max(1, static_cast<int>(std::lround(size.height() * scale)))),
               QImage::Format_RGB32);
  image.fill(Qt::white);  // color-literal-ok: the paper
  if (sheet < 0 || sheet >= static_cast<int>(imposition.sheets.size())) {
    return image;
  }
  const BookSheet& printed = imposition.sheets[static_cast<std::size_t>(sheet)];
  const auto& side_slots = back ? printed.back : printed.front;
  const double cell_width = size.width() / std::max(1, imposition.columns);
  const double cell_height = size.height() / std::max(1, imposition.rows);
  BookPagePlacementOptions placement;
  placement.outer_margin = std::max(0.0, options.outer_margin_mm) * kPointsPerMm;
  placement.spine_margin = std::max(0.0, options.spine_margin_mm) * kPointsPerMm;
  placement.fill = options.fill;
  if (nested_style(options.style) && options.creep) {
    placement.creep_shift = printed.sheet_in_signature * std::max(0.0, options.paper_thickness_mm) * kPointsPerMm;
  }
  QPdfDocumentRenderOptions render_options;
  render_options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.scale(scale, scale);
  for (const auto& slot : side_slots) {
    if (slot.book_page < 0 || slot.book_page >= static_cast<int>(imposition.book_pages.size())) {
      continue;
    }
    const int index = imposition.book_pages[static_cast<std::size_t>(slot.book_page)];
    if (index < 0 || index >= static_cast<int>(source_pages.size())) {
      continue;  // a blank page
    }
    const BookSourcePage& source = source_pages[static_cast<std::size_t>(index)];
    const QSizeF page_points = document.pagePointSize(source.page);
    const BookRect cell{0.0, 0.0, cell_width, cell_height};
    const BookPagePlacement placed =
        place_book_page(cell, book_source_page_width(source.part, page_points.width()), page_points.height(),
                        book_binding_edge(slot.book_page, options.right_to_left), placement);
    const QSize pixels(std::max(1, static_cast<int>(std::lround(placed.page.width * scale))),
                       std::max(1, static_cast<int>(std::lround(placed.page.height * scale))));
    QPdfDocumentRenderOptions part_options = render_options;
    if (source.part != BookPagePart::Whole) {
      // Half of a spread: the whole page at twice the width, clipped to that half (the
      // output is the clip's size; docs/pdf-viewer.md pins these semantics).
      part_options.setScaledSize(QSize(pixels.width() * 2, pixels.height()));
      part_options.setScaledClipRect(
          QRect(source.part == BookPagePart::Left ? 0 : pixels.width(), 0, pixels.width(), pixels.height()));
    }
    const QImage rendered = document.render(source.page, pixels, part_options);
    painter.save();
    painter.translate(slot.column * cell_width, slot.row * cell_height);
    if (slot.upside_down) {
      painter.translate(cell_width, cell_height);
      painter.rotate(180.0);
    }
    painter.setClipRect(QRectF(placed.clip.x, placed.clip.y, placed.clip.width, placed.clip.height));
    if (!rendered.isNull()) {
      painter.drawImage(QRectF(placed.page.x, placed.page.y, placed.page.width, placed.page.height), rendered);
    }
    const bool cover = slot.book_page == 0 || slot.book_page == static_cast<int>(imposition.book_pages.size()) - 1;
    if (options.page_numbers == BookPageNumbers::All ||
        (options.page_numbers == BookPageNumbers::SkipCovers && !cover)) {
      painter.setClipping(false);
      draw_page_number(painter, placed, cell, slot.book_page + 1);
    }
    painter.restore();
  }
  draw_marks(painter, options, imposition, printed, size, back);
  painter.end();
  if (back && options.turn_backs) {
    image = turned_half(image);
  }
  return image;
}

BookAssemblyGuide book_assembly_guide(const PdfBookPrintOptions& options, const BookImposition& imposition,
                                      const std::vector<BookSourcePage>& source_pages) {
  BookAssemblyGuide guide;
  const int sheets = static_cast<int>(imposition.sheets.size());
  if (sheets == 0) {
    return guide;
  }
  const auto add = [&guide](QString title, QStringList details) {
    guide.steps.push_back({std::move(title), std::move(details)});
  };
  if (imposition.two_sided) {
    if (options.manual_duplex) {
      add(QObject::tr("Print %n sheet(s), one side at a time", nullptr, sheets),
          {QObject::tr("Patchy prints the first side of every sheet, then asks you to put the stack back in the "
                       "paper tray."),
           QObject::tr("Try one sheet first: mark the top edge of a blank sheet, print, and see which way it comes "
                       "out, so you know how to reload the stack.")});
    } else {
      add(QObject::tr("Print %n sheet(s) on both sides", nullptr, sheets),
          {QObject::tr("Patchy asks the printer to flip on the short edge."),
           QObject::tr("If the print dialog has a two-sided setting, choose short-edge binding (sometimes called "
                       "\"flip up\").")});
    }
    const auto& first = imposition.sheets.front();
    add(QObject::tr("Check sheet 1"),
        {QObject::tr("One side shows %1 and %2 side by side.")
             .arg(page_name(imposition, source_pages, slot_page(first.front, 0)),
                  page_name(imposition, source_pages, slot_page(first.front, 1))),
         QObject::tr("The other side shows %1 and %2.")
             .arg(page_name(imposition, source_pages, slot_page(first.back, 0)),
                  page_name(imposition, source_pages, slot_page(first.back, 1))),
         QObject::tr("Second side upside down? Turn on \"Backs print upside down\" and print again.")});
  } else {
    add(QObject::tr("Print %n sheet(s) on one side only", nullptr, sheets), {});
  }
  switch (options.style) {
    case BookPrintStyle::Booklet: {
      add(QObject::tr("Stack the sheets"),
          {QObject::tr("Sheet 1, the one with the front cover, on top, then sheet 2, and so on."),
           QObject::tr("Every sheet the same way round, front cover side facing up."),
           QObject::tr("Tap the edges square.")});
      QStringList staple{QObject::tr("Use a long-reach stapler and staple from the top: two staples, a quarter of "
                                     "the way in from the top and bottom edges (three for a tall book).")};
      if (options.marks) {
        staple << QObject::tr("The marks at the top and bottom edges show the centre line.");
      }
      staple << QObject::tr("No long-reach stapler? Fold first, then open the booklet face down on thick "
                            "cardboard, staple through the fold into the cardboard, pull it free and press the "
                            "staple legs flat. Needle and thread through three holes in the fold work too.");
      add(QObject::tr("Staple along the centre line"), staple);
      add(QObject::tr("Fold it in half"),
          {QObject::tr("Fold the whole stack at once along the staples, front cover outside."),
           QObject::tr("Crease firmly with a ruler or the back of a spoon.")});
      add(QObject::tr("Trim (optional)"),
          {QObject::tr("The inner pages stick out a little at the open edge. Trim it straight with a ruler and "
                       "craft knife.")});
      guide.notes << QObject::tr("A booklet folds well up to about 12 sheets (48 pages) of ordinary paper. For a "
                                 "thicker book, choose Sewn book.");
      break;
    }
    case BookPrintStyle::Signatures: {
      add(QObject::tr("Split the stack into signatures"),
          {QObject::tr("Make %n signature(s) of %1 sheet(s) each, in the order they printed.", nullptr,
                       imposition.signatures)
               .arg(std::clamp(options.sheets_per_signature, 1, 16)),
           QObject::tr("The last one may have fewer sheets.")});
      add(QObject::tr("Fold each signature"),
          {QObject::tr("Its first sheet on top, the side with its first page facing up."),
           QObject::tr("Fold it in half with its first page outside, and crease it.")});
      QStringList stack{QObject::tr("In order, folds together."),
                        QObject::tr("Signature 1 on top when the book lies front cover up.")};
      if (options.marks && imposition.signatures > 1) {
        stack << QObject::tr("From the second signature on, each fold has a black mark one step lower than the "
                             "last. A mark out of line means a signature is out of order.");
      }
      add(QObject::tr("Stack the signatures"), stack);
      add(QObject::tr("Bind"),
          {QObject::tr("Sew: stitch each signature through its fold to its neighbours."),
           QObject::tr("Or glue: clamp the stack between two boards with the folds sticking out a little, brush "
                       "white glue (PVA) on the spine, and glue a cover around it once it is dry.")});
      add(QObject::tr("Trim"), {QObject::tr("Trim the open edge straight with a ruler and craft knife.")});
      break;
    }
    case BookPrintStyle::CutAndStack: {
      add(QObject::tr("Stack the sheets"),
          {QObject::tr("Sheet 1, the one with the front cover, on top, then sheet 2, and so on."),
           QObject::tr("Every sheet the same way round, front cover side facing up.")});
      QStringList cut{QObject::tr("Down the middle, with a paper trimmer or a ruler and craft knife.")};
      if (options.marks) {
        cut << QObject::tr("The marks at the top and bottom edges show the line.");
      }
      add(QObject::tr("Cut the stack in half"), cut);
      add(QObject::tr("Put the piles together"),
          {QObject::tr("Right-hand pile under the left-hand pile. The pages now read in order.")});
      add(options.right_to_left ? QObject::tr("Bind along the right edge") : QObject::tr("Bind along the left edge"),
          {QObject::tr("Glue: clamp the stack and brush two coats of white glue (PVA) on that edge."),
           QObject::tr("Or use comb or coil binding, or two or three staples close to the edge."),
           QObject::tr("For comb binding or staples, set a spine margin of at least 10 mm.")});
      break;
    }
    case BookPrintStyle::MiniZine:
      add(QObject::tr("Crease eight panels"),
          {QObject::tr("Fold in half from top edge to bottom edge, crease, and unfold."),
           QObject::tr("Fold in half from side to side, crease, and unfold."),
           QObject::tr("Fold each side in to the middle crease, crease, and unfold.")});
      add(QObject::tr("Cut the middle"),
          {QObject::tr("Fold in half from side to side again."),
           QObject::tr("From the folded edge, cut along the middle crease through the two middle panels only (the "
                       "printed line shows the cut).")});
      add(QObject::tr("Open it into a cross"),
          {QObject::tr("Unfold, then fold in half from top edge to bottom edge, printing outside."),
           QObject::tr("Push the two ends toward each other: the cut opens into a diamond, then a cross.")});
      add(QObject::tr("Fold the book"),
          {QObject::tr("Fold the four flaps together, front cover outside, and crease the spine.")});
      if (sheets > 1) {
        guide.notes << QObject::tr("Each sheet makes its own eight-page zine: pages 1 to 8, 9 to 16, and so on.");
      }
      break;
  }
  if (imposition.blank_pages > 0) {
    guide.notes << QObject::tr("%n blank page(s) were added to fill the last sheet.", nullptr, imposition.blank_pages);
  }
  return guide;
}

QString book_assembly_html(const BookAssemblyGuide& guide) {
  QString html = QStringLiteral("<ol>");
  for (const BookAssemblyStep& step : guide.steps) {
    html += QStringLiteral("<li style=\"margin-bottom:6px\"><b>") + step.title.toHtmlEscaped() +
            QStringLiteral("</b>");
    if (!step.details.isEmpty()) {
      html += QStringLiteral("<ul>");
      for (const QString& detail : step.details) {
        html += QStringLiteral("<li>") + detail.toHtmlEscaped() + QStringLiteral("</li>");
      }
      html += QStringLiteral("</ul>");
    }
    html += QStringLiteral("</li>");
  }
  html += QStringLiteral("</ol>");
  if (!guide.notes.isEmpty()) {
    html += QStringLiteral("<p><b>") + QObject::tr("Good to know").toHtmlEscaped() + QStringLiteral("</b></p><ul>");
    for (const QString& note : guide.notes) {
      html += QStringLiteral("<li>") + note.toHtmlEscaped() + QStringLiteral("</li>");
    }
    html += QStringLiteral("</ul>");
  }
  return html;
}

QString book_assembly_text(const BookAssemblyGuide& guide) {
  QStringList lines;
  for (std::size_t index = 0; index < guide.steps.size(); ++index) {
    lines << QStringLiteral("%1. %2").arg(index + 1).arg(guide.steps[index].title);
    for (const QString& detail : guide.steps[index].details) {
      lines << QStringLiteral("   - ") + detail;
    }
  }
  for (const QString& note : guide.notes) {
    lines << QStringLiteral("- ") + note;
  }
  return lines.join(QLatin1Char('\n'));
}

PdfBookWriteResult write_pdf_book(QPdfDocument& document, const PdfBookPrintOptions& options, const QString& path,
                                  const PdfBookProgress& progress) {
  PdfBookWriteResult result;
  const std::vector<BookSourcePage> source_pages = book_source_pages(options, book_pdf_page_sizes(document));
  const BookImposition imposition = book_imposition_for(options, source_pages);
  if (imposition.sheets.empty()) {
    result.error = QObject::tr("The document has no pages to print.");
    return result;
  }
  const std::vector<SideRef> sides = printed_sides(imposition, true, true);
  const int count = static_cast<int>(sides.size()) + (options.steps_page ? 1 : 0);
  PdfExportOptions encode;
  encode.auto_grayscale = true;
  if (!apply_pdf_image_quality(options.quality_id, encode)) {
    apply_pdf_image_quality(QString::fromLatin1(kDefaultPdfImageQualityId), encode);
  }
  const QSizeF sheet = book_sheet_points(options);
  const double dpi = std::clamp(options.dpi, 72, 1200);
  try {
    pdf::ImageWriter writer(to_filesystem_path(path));
    writer.set_document_info({{"Title", QFileInfo(path).completeBaseName().toStdString()}});
    int written = 0;
    const auto add = [&](const QImage& image) {
      writer.add_page(pdf_detail::encode_page_image(image, sheet.width(), sheet.height(), encode));
      ++written;
      return !progress || progress(written, count);
    };
    for (const SideRef& side : sides) {
      QImage image = wait_with_events(launch_async([&document, &options, &imposition, &source_pages, side, dpi] {
        return render_book_sheet_side(document, options, imposition, source_pages, side.sheet, side.back, dpi);
      }));
      if (!add(image)) {
        writer.abort();
        result.cancelled = true;
        return result;
      }
    }
    if (options.steps_page && !add(render_steps_page(options, book_assembly_guide(options, imposition, source_pages),
                                                     std::min(dpi, 300.0)))) {
      writer.abort();
      result.cancelled = true;
      return result;
    }
    writer.finish();
    result.ok = true;
    result.pages = written;
  } catch (const std::exception& error) {
    result.error = QString::fromStdString(error.what());
  }
  return result;
}

PdfBookPrintOptions load_pdf_book_print_options() {
  PdfBookPrintOptions options;
  const auto settings = app_settings();
  options.style = style_from_token(settings.value(settings_key("style")).toString());
  options.sheets_per_signature = std::clamp(settings.value(settings_key("sheetsPerSignature"), 4).toInt(), 1, 16);
  const QString default_paper =
      QLocale().measurementSystem() == QLocale::MetricSystem ? QStringLiteral("A4") : QStringLiteral("Letter");
  const QString paper = settings.value(settings_key("paper"), default_paper).toString();
  options.paper = QPageSize::Letter;
  for (const auto id : kPapers) {
    if (QPageSize::key(id) == paper) {
      options.paper = id;
    }
  }
  options.last_page_is_back_cover = settings.value(settings_key("backCover"), false).toBool();
  options.fill = settings.value(settings_key("scaling")).toString() == QStringLiteral("fill");
  options.outer_margin_mm = std::clamp(settings.value(settings_key("outerMarginMm"), 5.0).toDouble(), 0.0, 50.0);
  options.spine_margin_mm = std::clamp(settings.value(settings_key("spineMarginMm"), 0.0).toDouble(), 0.0, 50.0);
  options.creep = settings.value(settings_key("creep"), true).toBool();
  options.paper_thickness_mm =
      std::clamp(settings.value(settings_key("paperThicknessMm"), 0.1).toDouble(), 0.02, 1.0);
  options.marks = settings.value(settings_key("marks"), true).toBool();
  options.turn_backs = settings.value(settings_key("turnBacks"), false).toBool();
  options.manual_duplex = settings.value(settings_key("manualDuplex"), false).toBool();
  options.reverse_backs = settings.value(settings_key("reverseBacks"), false).toBool();
  const int dpi = settings.value(settings_key("dpi"), 300).toInt();
  options.dpi = std::find(kResolutions.begin(), kResolutions.end(), dpi) != kResolutions.end() ? dpi : 300;
  options.steps_page = settings.value(settings_key("stepsPage"), false).toBool();
  options.page_numbers = page_numbers_from_token(settings.value(settings_key("pageNumbers")).toString());
  options.quality_id =
      settings.value(settings_key("quality"), QString::fromLatin1(kDefaultPdfImageQualityId)).toString();
  return options;
}

void store_pdf_book_print_options(const PdfBookPrintOptions& options) {
  auto settings = app_settings();
  settings.setValue(settings_key("style"), style_token(options.style));
  settings.setValue(settings_key("sheetsPerSignature"), options.sheets_per_signature);
  settings.setValue(settings_key("paper"), QPageSize::key(options.paper));
  settings.setValue(settings_key("backCover"), options.last_page_is_back_cover);
  settings.setValue(settings_key("scaling"), options.fill ? QStringLiteral("fill") : QStringLiteral("fit"));
  settings.setValue(settings_key("outerMarginMm"), options.outer_margin_mm);
  settings.setValue(settings_key("spineMarginMm"), options.spine_margin_mm);
  settings.setValue(settings_key("creep"), options.creep);
  settings.setValue(settings_key("paperThicknessMm"), options.paper_thickness_mm);
  settings.setValue(settings_key("marks"), options.marks);
  settings.setValue(settings_key("turnBacks"), options.turn_backs);
  settings.setValue(settings_key("manualDuplex"), options.manual_duplex);
  settings.setValue(settings_key("reverseBacks"), options.reverse_backs);
  settings.setValue(settings_key("dpi"), options.dpi);
  settings.setValue(settings_key("stepsPage"), options.steps_page);
  settings.setValue(settings_key("pageNumbers"), page_numbers_token(options.page_numbers));
  settings.setValue(settings_key("quality"), options.quality_id);
}

namespace {

// Paints `sides` into one print job. False when the job could not start or was cancelled.
bool print_sides(QWidget* parent, QPrinter& printer, QPdfDocument& document, const PdfBookPrintOptions& options,
                 const BookImposition& imposition, const std::vector<BookSourcePage>& source_pages,
                 const std::vector<SideRef>& sides, const BookAssemblyGuide* steps) {
  printer.setFullPage(true);
  QPainter painter(&printer);
  if (!painter.isActive()) {
    return false;
  }
  QProgressDialog progress(QObject::tr("Preparing sheets..."), QObject::tr("Cancel"), 0,
                           static_cast<int>(sides.size()) + (steps == nullptr ? 0 : 1), parent);
  progress.setObjectName(QStringLiteral("pdfBookPrintProgressDialog"));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(300);
  const double dpi = std::clamp(options.dpi, 72, 1200);
  bool first = true;
  const auto paint = [&](const QImage& image) {
    if (!first && !printer.newPage()) {
      return false;
    }
    first = false;
    const QRect full = printer.pageLayout().fullRectPixels(std::max(1, printer.resolution()));
    painter.drawImage(QRectF(full), image);
    return true;
  };
  int done = 0;
  for (const SideRef& side : sides) {
    progress.setLabelText(QObject::tr("Preparing side %1 of %2...").arg(done + 1).arg(sides.size()));
    QImage image = wait_with_events(launch_async([&document, &options, &imposition, &source_pages, side, dpi] {
      return render_book_sheet_side(document, options, imposition, source_pages, side.sheet, side.back, dpi);
    }));
    if (progress.wasCanceled()) {
      printer.abort();
      return false;
    }
    if (!paint(image)) {
      return false;
    }
    progress.setValue(++done);
  }
  if (steps != nullptr) {
    if (!paint(render_steps_page(options, *steps, std::min(dpi, 300.0)))) {
      return false;
    }
    progress.setValue(++done);
  }
  painter.end();
  return true;
}

QString default_book_pdf_path(const QString& source) {
  const QFileInfo info(source);
  return info.dir().filePath(QObject::tr("%1 (book).pdf").arg(info.completeBaseName()));
}

}  // namespace

void run_pdf_book_print_dialog(QWidget* parent, QPdfDocument& document, const QString& path,
                               const PdfBookPrintStart& start) {
  const int page_count = document.pageCount();
  if (page_count <= 0) {
    return;
  }
  PdfBookPrintOptions options = load_pdf_book_print_options();
  options.right_to_left = start.right_to_left;
  if (start.cover_alone) {
    options.last_page_is_back_cover = true;
  }
  options.first_page = 0;
  options.last_page = -1;

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("pdfBookPrintDialog"));
  dialog.setWindowTitle(QObject::tr("Print as Book - %1").arg(QFileInfo(path).fileName()));
  auto* root = new QHBoxLayout(&dialog);

  auto* controls = new QVBoxLayout();
  auto* book_group = new QGroupBox(QObject::tr("Book"), &dialog);
  auto* book_form = new QFormLayout(book_group);
  auto* style = new QComboBox(book_group);
  style->setObjectName(QStringLiteral("pdfBookPrintStyleCombo"));
  style->addItem(QObject::tr("Booklet (fold and staple)"), style_token(BookPrintStyle::Booklet));
  style->addItem(QObject::tr("Sewn book (folded signatures)"), style_token(BookPrintStyle::Signatures));
  style->addItem(QObject::tr("Cut and stack (glue, comb or staple binding)"),
                 style_token(BookPrintStyle::CutAndStack));
  style->addItem(QObject::tr("Mini zine (8 pages from one sheet)"), style_token(BookPrintStyle::MiniZine));
  style->setItemData(0, QObject::tr("Every sheet nested and folded once. Best up to about 48 pages."),
                     Qt::ToolTipRole);
  style->setItemData(1, QObject::tr("Groups of nested sheets, each folded on its own, then sewn or glued. For "
                                    "longer books."),
                     Qt::ToolTipRole);
  style->setItemData(2, QObject::tr("Two pages per side, cut down the middle and stacked into single leaves."),
                     Qt::ToolTipRole);
  style->setItemData(3, QObject::tr("One side of one sheet folds into an eight-page booklet with a single cut."),
                     Qt::ToolTipRole);
  style->setCurrentIndex(std::max(0, style->findData(style_token(options.style))));
  book_form->addRow(QObject::tr("Style:"), style);
  auto* signature_sheets = new QSpinBox(book_group);
  signature_sheets->setObjectName(QStringLiteral("pdfBookPrintSignatureSheetsSpin"));
  signature_sheets->setRange(1, 16);
  signature_sheets->setValue(options.sheets_per_signature);
  auto* signature_label = new QLabel(QObject::tr("Sheets per signature:"), book_group);
  book_form->addRow(signature_label, signature_sheets);
  auto* direction = new QComboBox(book_group);
  direction->setObjectName(QStringLiteral("pdfBookPrintDirectionCombo"));
  direction->addItem(QObject::tr("Left to right (binding on the left)"), QStringLiteral("ltr"));
  direction->addItem(QObject::tr("Right to left (binding on the right)"), QStringLiteral("rtl"));
  direction->setCurrentIndex(options.right_to_left ? 1 : 0);
  book_form->addRow(QObject::tr("Reading:"), direction);
  auto* back_cover = new QCheckBox(QObject::tr("Keep the last page as the back cover"), book_group);
  back_cover->setObjectName(QStringLiteral("pdfBookPrintBackCoverCheck"));
  back_cover->setToolTip(QObject::tr("Blank pages needed to fill the last sheet go before the last page"));
  back_cover->setChecked(options.last_page_is_back_cover);
  book_form->addRow(back_cover);
  controls->addWidget(book_group);

  auto* paper_group = new QGroupBox(QObject::tr("Paper and pages"), &dialog);
  auto* paper_form = new QFormLayout(paper_group);
  auto* paper = new QComboBox(paper_group);
  paper->setObjectName(QStringLiteral("pdfBookPrintPaperCombo"));
  for (const auto id : kPapers) {
    paper->addItem(QPageSize::name(id), QPageSize::key(id));
  }
  paper->setCurrentIndex(std::max(0, paper->findData(QPageSize::key(options.paper))));
  paper_form->addRow(QObject::tr("Sheet:"), paper);
  auto* range_row = new QHBoxLayout();
  auto* first_page = new QSpinBox(paper_group);
  first_page->setObjectName(QStringLiteral("pdfBookPrintFirstPageSpin"));
  first_page->setRange(1, page_count);
  first_page->setValue(1);
  auto* last_page = new QSpinBox(paper_group);
  last_page->setObjectName(QStringLiteral("pdfBookPrintLastPageSpin"));
  last_page->setRange(1, page_count);
  last_page->setValue(page_count);
  range_row->addWidget(first_page);
  auto* to_label = new QLabel(QObject::tr("to"), paper_group);
  to_label->setProperty(kScrubLabelExemptProperty, true);
  range_row->addWidget(to_label);
  range_row->addWidget(last_page);
  range_row->addWidget(new QLabel(QObject::tr("of %1").arg(page_count), paper_group));
  range_row->addStretch(1);
  paper_form->addRow(QObject::tr("Pages:"), range_row);
  // A scanned booklet laid flat has two book pages on every PDF page. Not remembered: it
  // describes this file, and splitting another file's wide pages would be a surprise.
  auto* spreads = new QComboBox(paper_group);
  spreads->setObjectName(QStringLiteral("pdfBookPrintSpreadsCombo"));
  spreads->addItem(QObject::tr("None (one book page per PDF page)"), QStringLiteral("none"));
  spreads->addItem(QObject::tr("Split wide pages in two"), QStringLiteral("split"));
  spreads->addItem(QObject::tr("Split, first page is the cover spread"), QStringLiteral("cover"));
  spreads->setItemData(1, QObject::tr("Every page wider than tall is cut down the middle into a left and a right "
                                      "page. Tall pages stay whole."),
                       Qt::ToolTipRole);
  spreads->setItemData(2, QObject::tr("As above, and the first page is the outside of the cover laid flat: the back "
                                      "cover beside the front cover. The back cover becomes the last page."),
                       Qt::ToolTipRole);
  spreads->setToolTip(QObject::tr("For PDFs with two book pages side by side on each page, such as a scanned "
                                  "booklet laid flat"));
  paper_form->addRow(QObject::tr("Two-page spreads:"), spreads);
  const std::vector<BookPageSize> page_sizes = book_pdf_page_sizes(document);
  auto* scaling = new QComboBox(paper_group);
  scaling->setObjectName(QStringLiteral("pdfBookPrintScalingCombo"));
  scaling->addItem(QObject::tr("Fit the whole page"), QStringLiteral("fit"));
  scaling->addItem(QObject::tr("Fill the space (crops the edges)"), QStringLiteral("fill"));
  scaling->setCurrentIndex(options.fill ? 1 : 0);
  paper_form->addRow(QObject::tr("Scaling:"), scaling);
  auto* page_numbers = new QComboBox(paper_group);
  page_numbers->setObjectName(QStringLiteral("pdfBookPrintPageNumbersCombo"));
  page_numbers->addItem(QObject::tr("None"), page_numbers_token(BookPageNumbers::None));
  page_numbers->addItem(QObject::tr("Every page but the covers"), page_numbers_token(BookPageNumbers::SkipCovers));
  page_numbers->addItem(QObject::tr("Every page"), page_numbers_token(BookPageNumbers::All));
  page_numbers->setToolTip(QObject::tr("Prints each page's number in the book under it. Blank pages added to fill "
                                       "the last sheet get no number."));
  page_numbers->setCurrentIndex(std::max(0, page_numbers->findData(page_numbers_token(options.page_numbers))));
  paper_form->addRow(QObject::tr("Page numbers:"), page_numbers);
  const auto make_mm_spin = [&paper_group](const char* name, double value, double maximum, double step,
                                           int decimals) {
    auto* spin = new QDoubleSpinBox(paper_group);
    spin->setObjectName(QLatin1String(name));
    spin->setRange(0.0, maximum);
    spin->setDecimals(decimals);
    spin->setSingleStep(step);
    spin->setSuffix(QObject::tr(" mm"));
    spin->setValue(value);
    return spin;
  };
  auto* outer_margin = make_mm_spin("pdfBookPrintOuterMarginSpin", options.outer_margin_mm, 50.0, 1.0, 1);
  outer_margin->setToolTip(QObject::tr("Space at the top, bottom and outer edge of every page"));
  paper_form->addRow(QObject::tr("Outer margin:"), outer_margin);
  auto* spine_margin = make_mm_spin("pdfBookPrintSpineMarginSpin", options.spine_margin_mm, 50.0, 1.0, 1);
  spine_margin->setToolTip(
      QObject::tr("Space at the fold or binding edge. Zero lets a picture across two pages meet at the fold."));
  paper_form->addRow(QObject::tr("Spine margin:"), spine_margin);
  auto* creep = new QCheckBox(QObject::tr("Shift inner sheets for paper thickness"), paper_group);
  creep->setObjectName(QStringLiteral("pdfBookPrintCreepCheck"));
  creep->setToolTip(QObject::tr("Nested sheets push the inner pages outward when folded (creep). Each inner "
                                "sheet's pages move toward the fold by one paper thickness."));
  creep->setChecked(options.creep);
  auto* thickness = make_mm_spin("pdfBookPrintThicknessSpin", options.paper_thickness_mm, 1.0, 0.01, 2);
  thickness->setMinimum(0.02);
  thickness->setToolTip(QObject::tr("Ordinary 80 g/m2 (20 lb) office paper is about 0.1 mm thick"));
  auto* creep_row = new QHBoxLayout();
  creep_row->addWidget(creep);
  creep_row->addWidget(thickness);
  creep_row->addStretch(1);
  paper_form->addRow(creep_row);
  controls->addWidget(paper_group);

  auto* print_group = new QGroupBox(QObject::tr("Printing"), &dialog);
  auto* print_form = new QFormLayout(print_group);
  auto* marks = new QCheckBox(QObject::tr("Print fold and cut marks"), print_group);
  marks->setObjectName(QStringLiteral("pdfBookPrintMarksCheck"));
  marks->setChecked(options.marks);
  print_form->addRow(marks);
  auto* turn_backs = new QCheckBox(QObject::tr("Backs print upside down"), print_group);
  turn_backs->setObjectName(QStringLiteral("pdfBookPrintTurnBacksCheck"));
  turn_backs->setToolTip(QObject::tr("Turn on when your printer flips on the long edge and the second side "
                                     "comes out upside down"));
  turn_backs->setChecked(options.turn_backs);
  print_form->addRow(turn_backs);
  auto* manual = new QCheckBox(QObject::tr("Print the second side by hand"), print_group);
  manual->setObjectName(QStringLiteral("pdfBookPrintManualDuplexCheck"));
  manual->setToolTip(QObject::tr("For printers that cannot print both sides: all first sides, then you reload "
                                 "the stack for the second sides"));
  manual->setChecked(options.manual_duplex);
  print_form->addRow(manual);
  auto* reverse_backs = new QCheckBox(QObject::tr("Print the second sides last sheet first"), print_group);
  reverse_backs->setObjectName(QStringLiteral("pdfBookPrintReverseBacksCheck"));
  reverse_backs->setToolTip(QObject::tr("Turn on when the reloaded stack has the last sheet on top"));
  reverse_backs->setChecked(options.reverse_backs);
  print_form->addRow(reverse_backs);
  auto* resolution = new QComboBox(print_group);
  resolution->setObjectName(QStringLiteral("pdfBookPrintResolutionCombo"));
  for (const int dpi : kResolutions) {
    resolution->addItem(QObject::tr("%1 dpi").arg(dpi), dpi);
  }
  resolution->setCurrentIndex(std::max(0, resolution->findData(options.dpi)));
  print_form->addRow(QObject::tr("Resolution:"), resolution);
  auto* quality = new QComboBox(print_group);
  quality->setObjectName(QStringLiteral("pdfBookPrintQualityCombo"));
  populate_pdf_image_quality_combo(*quality, options.quality_id);
  print_form->addRow(QObject::tr("Saved PDF quality:"), quality);
  auto* steps_page = new QCheckBox(QObject::tr("Add a page with the assembly steps"), print_group);
  steps_page->setObjectName(QStringLiteral("pdfBookPrintStepsPageCheck"));
  steps_page->setChecked(options.steps_page);
  print_form->addRow(steps_page);
  controls->addWidget(print_group);
  controls->addStretch(1);
  root->addLayout(controls);

  auto* preview_column = new QVBoxLayout();
  auto* summary = new QLabel(&dialog);
  summary->setObjectName(QStringLiteral("pdfBookPrintSummaryLabel"));
  summary->setWordWrap(true);
  preview_column->addWidget(summary);
  auto* sheet_row = new QHBoxLayout();
  sheet_row->addWidget(new QLabel(QObject::tr("Sheet:"), &dialog));
  auto* sheet_spin = new QSpinBox(&dialog);
  sheet_spin->setObjectName(QStringLiteral("pdfBookPrintSheetSpin"));
  sheet_spin->setMinimum(1);
  sheet_row->addWidget(sheet_spin);
  sheet_row->addStretch(1);
  preview_column->addLayout(sheet_row);
  constexpr int kPreviewWidth = 300;
  // The two sides side by side, so the steps below keep their room on a short screen.
  auto* previews = new QGridLayout();
  auto* front_title = new QLabel(QObject::tr("First side"), &dialog);
  previews->addWidget(front_title, 0, 0);
  auto* front_preview = new QLabel(&dialog);
  front_preview->setObjectName(QStringLiteral("pdfBookPrintFrontPreview"));
  front_preview->setMinimumSize(kPreviewWidth, kPreviewWidth * 4 / 5);
  front_preview->setAlignment(Qt::AlignCenter);
  previews->addWidget(front_preview, 1, 0);
  auto* back_title = new QLabel(QObject::tr("Second side (turned over side to side)"), &dialog);
  previews->addWidget(back_title, 0, 1);
  auto* back_preview = new QLabel(&dialog);
  back_preview->setObjectName(QStringLiteral("pdfBookPrintBackPreview"));
  back_preview->setMinimumSize(kPreviewWidth, kPreviewWidth * 4 / 5);
  back_preview->setAlignment(Qt::AlignCenter);
  previews->addWidget(back_preview, 1, 1);
  preview_column->addLayout(previews);
  auto* steps_view = new QTextBrowser(&dialog);
  steps_view->setObjectName(QStringLiteral("pdfBookPrintStepsView"));
  steps_view->setOpenLinks(false);
  steps_view->setMinimumHeight(160);
  preview_column->addWidget(new QLabel(QObject::tr("Putting the book together"), &dialog));
  preview_column->addWidget(steps_view, 1);
  auto* buttons = new QDialogButtonBox(&dialog);
  auto* print_button = buttons->addButton(QObject::tr("Print..."), QDialogButtonBox::AcceptRole);
  print_button->setObjectName(QStringLiteral("pdfBookPrintPrintButton"));
  auto* save_button = buttons->addButton(QObject::tr("Save PDF..."), QDialogButtonBox::ActionRole);
  save_button->setObjectName(QStringLiteral("pdfBookPrintSaveButton"));
  auto* close_button = buttons->addButton(QDialogButtonBox::Close);
  close_button->setObjectName(QStringLiteral("pdfBookPrintCloseButton"));
  preview_column->addWidget(buttons);
  root->addLayout(preview_column, 1);

  // A cover spread makes its back cover the last page, so the back cover check is shown on
  // and locked while that is chosen; the user's own choice comes back after.
  bool back_cover_choice = options.last_page_is_back_cover;
  const auto read_options = [&] {
    PdfBookPrintOptions read = options;
    read.style = style_from_token(style->currentData().toString());
    read.sheets_per_signature = signature_sheets->value();
    read.right_to_left = direction->currentData().toString() == QStringLiteral("rtl");
    read.last_page_is_back_cover = back_cover->isEnabled() ? back_cover->isChecked() : back_cover_choice;
    const QString spreads_token = spreads->currentData().toString();
    read.spreads = spreads_token == QStringLiteral("split")   ? BookSpreads::Split
                   : spreads_token == QStringLiteral("cover") ? BookSpreads::SplitCoverSpread
                                                              : BookSpreads::None;
    const QString paper_key = paper->currentData().toString();
    for (const auto id : kPapers) {
      if (QPageSize::key(id) == paper_key) {
        read.paper = id;
      }
    }
    read.first_page = first_page->value() - 1;
    read.last_page = std::max(first_page->value(), last_page->value()) - 1;
    read.fill = scaling->currentData().toString() == QStringLiteral("fill");
    read.page_numbers = page_numbers_from_token(page_numbers->currentData().toString());
    read.outer_margin_mm = outer_margin->value();
    read.spine_margin_mm = spine_margin->value();
    read.creep = creep->isChecked();
    read.paper_thickness_mm = thickness->value();
    read.marks = marks->isChecked();
    read.turn_backs = turn_backs->isChecked();
    read.manual_duplex = manual->isChecked();
    read.reverse_backs = reverse_backs->isChecked();
    read.dpi = resolution->currentData().toInt();
    read.steps_page = steps_page->isChecked();
    read.quality_id = quality->currentData().toString();
    return read;
  };

  // The preview renders on a worker; a change while it runs asks for one more pass.
  struct PreviewState {
    std::future<std::pair<QImage, QImage>> running;
    bool again{false};
  };
  auto preview_state = std::make_shared<PreviewState>();
  const auto wait_preview = qScopeGuard([preview_state] {
    if (preview_state->running.valid()) {
      preview_state->running.wait();
    }
  });
  std::function<void()> refresh_preview;
  refresh_preview = [&] {
    if (preview_state->running.valid()) {
      preview_state->again = true;
      return;
    }
    PdfBookPrintOptions current = read_options();
    // The preview shows the second side the way it reads once the sheet is turned over,
    // never the upside-down print a long-edge printer needs.
    current.turn_backs = false;
    const std::vector<BookSourcePage> pages = book_source_pages(current, page_sizes);
    const BookImposition imposition = book_imposition_for(current, pages);
    const int sheet = sheet_spin->value() - 1;
    const double dpi = kPreviewWidth * front_preview->devicePixelRatioF() / (book_sheet_points(current).width() / 72.0);
    QPdfDocument* pdf = &document;
    preview_state->running = launch_async([pdf, current, imposition, pages, sheet, dpi] {
      QImage front = render_book_sheet_side(*pdf, current, imposition, pages, sheet, false, dpi);
      QImage back = imposition.two_sided ? render_book_sheet_side(*pdf, current, imposition, pages, sheet, true, dpi)
                                         : QImage();
      return std::make_pair(std::move(front), std::move(back));
    });
    auto* poll = new QTimer(&dialog);
    poll->setInterval(20);
    QObject::connect(poll, &QTimer::timeout, &dialog, [&, poll] {
      if (preview_state->running.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
      }
      poll->stop();
      poll->deleteLater();
      auto [front, back] = preview_state->running.get();
      const double ratio = front_preview->devicePixelRatioF();
      QPixmap front_pixmap = QPixmap::fromImage(front);
      front_pixmap.setDevicePixelRatio(ratio);
      front_preview->setPixmap(front_pixmap);
      if (!back.isNull()) {
        QPixmap back_pixmap = QPixmap::fromImage(back);
        back_pixmap.setDevicePixelRatio(ratio);
        back_preview->setPixmap(back_pixmap);
      } else {
        back_preview->clear();
      }
      if (preview_state->again) {
        preview_state->again = false;
        refresh_preview();
      }
    });
    poll->start();
  };

  const auto refresh = [&] {
    const bool cover_spread = spreads->currentData().toString() == QStringLiteral("cover");
    if (back_cover->isEnabled() == cover_spread) {
      const QSignalBlocker block(back_cover);
      back_cover->setEnabled(!cover_spread);
      back_cover->setChecked(cover_spread || back_cover_choice);
    }
    const PdfBookPrintOptions current = read_options();
    const std::vector<BookSourcePage> source_pages = book_source_pages(current, page_sizes);
    const BookImposition imposition = book_imposition_for(current, source_pages);
    const bool nested = nested_style(current.style);
    signature_label->setVisible(current.style == BookPrintStyle::Signatures);
    signature_sheets->setVisible(current.style == BookPrintStyle::Signatures);
    signature_sheets->setSuffix(QObject::tr(" (%n page(s))", nullptr, signature_sheets->value() * 4));
    creep->setEnabled(nested);
    thickness->setEnabled(nested && creep->isChecked());
    turn_backs->setEnabled(imposition.two_sided);
    manual->setEnabled(imposition.two_sided);
    reverse_backs->setEnabled(imposition.two_sided && manual->isChecked());
    back_title->setVisible(imposition.two_sided);
    back_preview->setVisible(imposition.two_sided);
    const int sheets = static_cast<int>(imposition.sheets.size());
    sheet_spin->setMaximum(std::max(1, sheets));
    sheet_spin->setSuffix(QObject::tr(" of %1").arg(sheets));
    const int pages = static_cast<int>(source_pages.size());
    QString text = QObject::tr("%n page(s) on %1 sheet(s)", nullptr, pages).arg(sheets);
    if (current.style == BookPrintStyle::Signatures) {
      text += QStringLiteral(", ") + QObject::tr("%n signature(s)", nullptr, imposition.signatures);
    }
    if (imposition.blank_pages > 0) {
      text += QStringLiteral(", ") + QObject::tr("%n blank page(s) added", nullptr, imposition.blank_pages);
    }
    // Every page printed is wide: most likely two pages side by side, which would print four
    // to a sheet. Point at the choice that splits them.
    if (current.spreads == BookSpreads::None && !source_pages.empty() &&
        std::all_of(source_pages.begin(), source_pages.end(), [&page_sizes](const BookSourcePage& source) {
          return book_page_is_spread(page_sizes[static_cast<std::size_t>(source.page)]);
        })) {
      text += QStringLiteral("\n") + QObject::tr("The pages are wider than tall. If each holds two book pages, "
                                                 "choose a Split option under Two-page spreads.");
    }
    summary->setText(text);
    steps_view->setHtml(book_assembly_html(book_assembly_guide(current, imposition, source_pages)));
    refresh_preview();
  };

  QObject::connect(back_cover, &QCheckBox::toggled, &dialog, [&back_cover_choice](bool on) { back_cover_choice = on; });
  for (auto* combo : {style, direction, paper, spreads, scaling, page_numbers, resolution, quality}) {
    QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog, refresh);
  }
  for (auto* spin : {signature_sheets, first_page, last_page, sheet_spin}) {
    QObject::connect(spin, &QSpinBox::valueChanged, &dialog, refresh);
  }
  for (auto* spin : {outer_margin, spine_margin, thickness}) {
    QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, refresh);
  }
  for (auto* check : {back_cover, creep, marks, turn_backs, manual, reverse_backs, steps_page}) {
    QObject::connect(check, &QCheckBox::toggled, &dialog, refresh);
  }

  QObject::connect(close_button, &QPushButton::clicked, &dialog, &QDialog::reject);
  QObject::connect(save_button, &QPushButton::clicked, &dialog, [&] {
    const PdfBookPrintOptions current = read_options();
    store_pdf_book_print_options(current);
    const QString target = get_save_file_name(
        &dialog, QObject::tr("Save Book PDF"), default_book_pdf_path(path),
        QStringLiteral("%1 (*.pdf)").arg(QCoreApplication::translate("QObject", "PDF Document")), nullptr,
        QStringLiteral("pdfBookPrintSaveFileDialog"));
    if (target.isEmpty()) {
      return;
    }
    QProgressDialog progress(QObject::tr("Writing the book..."), QObject::tr("Cancel"), 0, 1, &dialog);
    progress.setObjectName(QStringLiteral("pdfBookPrintSaveProgressDialog"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    const PdfBookWriteResult result = write_pdf_book(document, current, target, [&progress](int done, int count) {
      progress.setMaximum(count);
      progress.setValue(done);
      return !progress.wasCanceled();
    });
    progress.reset();
    if (!result.ok && !result.cancelled) {
      QMessageBox box(QMessageBox::Warning, QObject::tr("Save Book PDF"),
                      QObject::tr("The book could not be saved: %1").arg(result.error), QMessageBox::Ok, &dialog);
      box.setObjectName(QStringLiteral("pdfBookPrintSaveFailedMessageBox"));
      exec_dialog(box);
    }
  });
  QObject::connect(print_button, &QPushButton::clicked, &dialog, [&] {
    const PdfBookPrintOptions current = read_options();
    store_pdf_book_print_options(current);
    const std::vector<BookSourcePage> pages = book_source_pages(current, page_sizes);
    const BookImposition imposition = book_imposition_for(current, pages);
    const BookAssemblyGuide guide = book_assembly_guide(current, imposition, pages);
    const BookAssemblyGuide* steps = current.steps_page ? &guide : nullptr;
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QFileInfo(path).completeBaseName());
    printer.setCreator(QStringLiteral("Patchy"));
    printer.setPageSize(QPageSize(current.paper));
    printer.setPageOrientation(QPageLayout::Landscape);
    printer.setPageMargins(QMarginsF(0.0, 0.0, 0.0, 0.0));
    const bool two_sided = imposition.two_sided && !current.manual_duplex;
    printer.setDuplex(two_sided ? QPrinter::DuplexShortSide : QPrinter::DuplexNone);
    QPrintDialog system_print(&printer, &dialog);
    system_print.setObjectName(QStringLiteral("pdfBookPrintSystemPrintDialog"));
    // The sheets are the pages: a page range would split folded pairs.
    system_print.setOption(QAbstractPrintDialog::PrintPageRange, false);
    if (exec_dialog(system_print) != QDialog::Accepted) {
      return;
    }
    // A driver dialog can switch back to portrait; the sheets are always landscape.
    printer.setPageOrientation(QPageLayout::Landscape);
    if (!current.manual_duplex || !imposition.two_sided) {
      print_sides(&dialog, printer, document, current, imposition, pages, printed_sides(imposition, true, true),
                  steps);
      return;
    }
    if (!print_sides(&dialog, printer, document, current, imposition, pages, printed_sides(imposition, true, false),
                     nullptr)) {
      return;
    }
    QMessageBox reload(QMessageBox::Information, QObject::tr("Print the Second Sides"),
                       QObject::tr("When the first sides have printed, put the whole stack back in the paper tray "
                                   "to print on the blank side, then click Print Second Sides."),
                       QMessageBox::NoButton, &dialog);
    reload.setObjectName(QStringLiteral("pdfBookPrintReloadMessageBox"));
    auto* go = reload.addButton(QObject::tr("Print Second Sides"), QMessageBox::AcceptRole);
    reload.addButton(QMessageBox::Cancel);
    exec_dialog(reload);
    if (reload.clickedButton() != go) {
      return;
    }
    std::vector<SideRef> backs = printed_sides(imposition, false, true);
    if (current.reverse_backs) {
      std::reverse(backs.begin(), backs.end());
    }
    print_sides(&dialog, printer, document, current, imposition, pages, backs, steps);
  });

  dialog.resize(1100, 720);
  refresh();
  exec_dialog(dialog);
  store_pdf_book_print_options(read_options());
}

}  // namespace patchy::ui
