#include "ui/pdf_text_selection.hpp"

#include "ui/pdf_ocr_text.hpp"

#include "ui/background_workers.hpp"

#include <QChar>
#include <QElapsedTimer>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace patchy::ui {

namespace {

using Piece = PdfTextSelection::Piece;
using PiecePtr = PdfTextSelection::PiecePtr;
using AreasPtr = PdfTextSelection::AreasPtr;
using Request = PdfTextSelection::Request;

// A task that only reads the text areas of pages gives the PDFium lock back after about
// this long, so a page render never waits long behind it.
constexpr qint64 kSliceMs = 12;
// How many characters from the start of a page are tried for one with a box of its own.
constexpr int kBaseProbes = 64;
// A double-click reads this many characters either side of the pointer in one call, and
// follows a word no further than kWordReach characters either side.
constexpr int kWordWindow = 48;
constexpr int kWordReach = 96;
// Qt PDF looks for a character within 8 points of a point (CharacterHitTolerance / 2).
constexpr double kHitReach = 8.0;
// A line piece is the height of its letters. The pointer counts as on the line this share
// of that height above and below it too, so the gaps between the lines of a paragraph are
// text (a press there selects) and the wider gap between paragraphs is not.
constexpr double kLineGapShare = 0.35;

bool is_word_character(char32_t c) {
  return QChar::isLetterOrNumber(c) || QChar::isMark(c) || c == U'_';
}

char32_t first_code_point(const QString& text) {
  if (text.isEmpty()) {
    return 0;
  }
  const char16_t unit = text[0].unicode();
  if (QChar::isHighSurrogate(unit) && text.size() > 1 && QChar::isLowSurrogate(text[1].unicode())) {
    return QChar::surrogateToUcs4(unit, text[1].unicode());
  }
  return unit;
}

QList<QRectF> solid_rects(const QPdfSelection& selection) {
  QList<QRectF> rects;
  for (const QPolygonF& polygon : selection.bounds()) {
    const QRectF rect = polygon.boundingRect();
    if (rect.width() > 0.0 && rect.height() > 0.0) {
      rects.append(rect);
    }
  }
  return rects;
}

// The line piece a point belongs to: the one whose row it is in or nearest to, then the
// nearest across, so a pointer beside a line or between two lines takes that line's end.
int nearest_piece(const PdfPageTextAreas& areas, QPointF point) {
  int best = -1;
  double best_dy = 0.0;
  double best_dx = 0.0;
  for (int index = 0; index < areas.pieces.size(); ++index) {
    const QRectF& piece = areas.pieces[index];
    const double dy = std::max({piece.top() - point.y(), 0.0, point.y() - piece.bottom()});
    const double dx = std::max({piece.left() - point.x(), 0.0, point.x() - piece.right()});
    if (best < 0 || dy < best_dy - 1e-6 || (dy <= best_dy + 1e-6 && dx < best_dx)) {
      best = index;
      best_dy = dy;
      best_dx = dx;
    }
  }
  return best;
}

// A point of `piece` at the place of `point` along the line, on the line's middle: inside
// the letters, where the neighbouring lines are furthest. A piece much taller than wide is
// text set down the page.
QPointF point_in_piece(const QRectF& piece, QPointF point) {
  if (piece.height() > 2.0 * piece.width()) {
    const double inset = std::min(0.25, piece.height() / 4.0);
    return {piece.center().x(), std::clamp(point.y(), piece.top() + inset, piece.bottom() - inset)};
  }
  const double inset = std::min(0.25, piece.width() / 4.0);
  return {std::clamp(point.x(), piece.left() + inset, piece.right() - inset), piece.center().y()};
}

AreasPtr read_areas(QPdfDocument* document, int page) {
  auto areas = std::make_shared<PdfPageTextAreas>();
  try {
    const QPdfSelection all = document->getAllText(page);
    areas->text = all.text();
    areas->char_count = areas->text.isEmpty() ? 0 : std::max(0, all.endIndex());
    areas->pieces = solid_rects(all);
    if (!areas->pieces.isEmpty()) {
      const int probes = std::min(areas->char_count, kBaseProbes);
      for (int index = 0; index < probes && areas->base_index < 0; ++index) {
        const QList<QRectF> boxes = solid_rects(document->getSelectionAtIndex(page, index, 1));
        if (!boxes.isEmpty()) {
          areas->base_index = index;
          areas->base_hit = index;
          areas->base_box = boxes.constFirst();
        }
      }
    }
    if (areas->base_index >= 0) {
      // Which character Qt PDF takes the middle of the base box for: the first character of a
      // selection from there to the end of the last line piece.
      const QRectF& last = areas->pieces.constLast();
      const QPdfSelection probe =
          document->getSelection(page, areas->base_box.center(), point_in_piece(last, last.bottomRight()));
      if (probe.isValid() && probe.startIndex() >= 0 && probe.startIndex() < probe.endIndex()) {
        areas->base_hit = probe.startIndex();
      }
    }
  } catch (...) {
    *areas = PdfPageTextAreas{};
  }
  return areas;
}

// The worker's view of the pages: what was known when the task started, and what it read.
struct Reader {
  QPdfDocument* document{nullptr};
  std::vector<AreasPtr> known;
  std::vector<std::pair<int, AreasPtr>> read;

  [[nodiscard]] bool valid_page(int page) const { return page >= 0 && page < static_cast<int>(known.size()); }
  const PdfPageTextAreas& areas(int page) {
    auto& slot = known[static_cast<std::size_t>(page)];
    if (slot == nullptr) {
      slot = read_areas(document, page);
      read.emplace_back(page, slot);
    }
    return *slot;
  }
};

// The caret nearest `point` on a page: the index of the character the point is in when it
// is in the character's first half, one more in its second half. -1 on a page without text.
//
// Qt PDF has no call for the character under a point, only for the text between two points
// (QPdfDocument::getSelection): its first character is the one under the first point, and
// it ends before or behind the character under the second point by the half that point is
// in. Measured from the middle of the page's first character, the selection's end index is
// the caret. On the first character itself Qt PDF reports nothing, and that case is told by
// the character's box. Qt PDF works in whole points here (it hands every rectangle out
// through integer device coordinates), so a caret is true to about half a point.
int caret_at(QPdfDocument* document, int page, const PdfPageTextAreas& areas, QPointF point) {
  if (!areas.has_text()) {
    return -1;
  }
  if (areas.recognized != nullptr) {
    return areas.recognized->caret_at(point);
  }
  const int piece = nearest_piece(areas, point);
  if (piece < 0) {
    return -1;
  }
  const QPointF spot = point_in_piece(areas.pieces[piece], point);
  const QPdfSelection found = document->getSelection(page, areas.base_box.center(), spot);
  const int start = found.startIndex();
  const int end = found.endIndex();
  if (start == areas.base_hit && end > start) {
    return std::min(end, areas.char_count);
  }
  if (found.isValid() && start >= 0 && start < areas.base_hit) {
    // A character before the base one: the selection starts with it.
    return start;
  }
  const QRectF& box = areas.base_box;
  if (box.adjusted(-kHitReach, -kHitReach, kHitReach, kHitReach).contains(spot)) {
    return std::abs(box.right() - spot.x()) < std::abs(box.left() - spot.x()) ? areas.base_hit + 1 : areas.base_hit;
  }
  return -1;
}

PiecePtr piece_from(int page, int start, int count, const QPdfSelection& selection) {
  auto piece = std::make_shared<Piece>();
  piece->page = page;
  piece->start = start;
  piece->count = count;
  piece->text = selection.text();
  piece->rects = solid_rects(selection);
  return piece;
}

// Characters [start, start + count) of a page, cut to the page; nullptr when that is
// nothing. A piece the selection already has is kept, so a drag asks PDFium only about the
// page the pointer is on.
PiecePtr make_piece(Reader& reader, int page, int start, int count, const std::vector<PiecePtr>& previous) {
  const PdfPageTextAreas& areas = reader.areas(page);
  if (!areas.has_text()) {
    return nullptr;
  }
  start = std::clamp(start, 0, areas.char_count);
  count = std::clamp(count, 0, areas.char_count - start);
  if (count <= 0) {
    return nullptr;
  }
  for (const auto& old : previous) {
    if (old->page == page && old->start == start && old->count == count) {
      return old;
    }
  }
  if (start == 0 && count == areas.char_count) {
    auto piece = std::make_shared<Piece>();
    piece->page = page;
    piece->start = start;
    piece->count = count;
    piece->text = areas.text;
    piece->rects = areas.pieces;
    return piece;
  }
  if (areas.recognized != nullptr) {
    auto piece = std::make_shared<Piece>();
    piece->page = page;
    piece->start = start;
    piece->count = count;
    piece->text = areas.text.mid(start, count);
    piece->rects = areas.recognized->range_rects(start, count);
    return piece;
  }
  const QPdfSelection selection = reader.document->getSelectionAtIndex(page, start, count);
  auto piece = std::make_shared<Piece>();
  piece->page = page;
  piece->start = start;
  piece->count = count;
  piece->text = selection.text();
  piece->rects = solid_rects(selection);
  // Qt PDF measures the rectangles for as many characters as the text has UTF-16 units. Where
  // PDFium's text leaves a character out that is too few, and the last letters would go
  // untinted: ask for that many characters more until the units cover the range. The text
  // stays the first answer's.
  int asked = count;
  qsizetype units = piece->text.size();
  for (int round = 0; round < 4 && units < count && start + asked < areas.char_count; ++round) {
    asked = std::min(areas.char_count - start, asked + count - static_cast<int>(units));
    const QPdfSelection wider = reader.document->getSelectionAtIndex(page, start, asked);
    units = wider.text().size();
    if (units <= count) {
      piece->rects = solid_rects(wider);
    }
  }
  return piece;
}

struct Answer {
  // False when the question has no answer (a press on a page without text): the selection
  // stays as it is.
  bool applies{false};
  std::optional<PdfTextCaret> anchor;
  std::optional<PdfTextCaret> anchor_far;
  std::vector<PiecePtr> pieces;
  std::vector<std::pair<int, AreasPtr>> read;
};

void add_range(Reader& reader, PdfTextCaret from, PdfTextCaret to, const std::vector<PiecePtr>& previous,
               const std::atomic<bool>& cancel, std::vector<PiecePtr>& pieces) {
  const auto add = [&](int page, int start, int count) {
    if (auto piece = make_piece(reader, page, start, count, previous); piece != nullptr) {
      pieces.push_back(std::move(piece));
    }
  };
  constexpr int kToEnd = std::numeric_limits<int>::max();
  if (from.page == to.page) {
    add(from.page, from.index, to.index - from.index);
    return;
  }
  add(from.page, from.index, kToEnd);
  for (int page = from.page + 1; page < to.page && !cancel.load(); ++page) {
    add(page, 0, kToEnd);
  }
  add(to.page, 0, to.index);
}

void answer_range(Reader& reader, const Request& request, const std::vector<PiecePtr>& previous,
                  const std::atomic<bool>& cancel, Answer& answer) {
  std::optional<PdfTextCaret> anchor = request.anchor;
  if (!anchor.has_value()) {
    const int page = request.anchor_origin.page;
    if (!reader.valid_page(page)) {
      return;
    }
    const int index = caret_at(reader.document, page, reader.areas(page), request.anchor_origin.point);
    if (index < 0) {
      return;
    }
    anchor = PdfTextCaret{page, index};
  }
  const int page = request.focus.page;
  if (!reader.valid_page(page)) {
    return;
  }
  std::optional<PdfTextCaret> focus;
  if (const int index = caret_at(reader.document, page, reader.areas(page), request.focus.point); index >= 0) {
    focus = PdfTextCaret{page, index};
  } else if (page > anchor->page) {
    // A page without text under the pointer: up to the end of the last page with text
    // before it.
    for (int other = page - 1; other >= anchor->page && !cancel.load(); --other) {
      if (reader.areas(other).has_text()) {
        focus = PdfTextCaret{other, reader.areas(other).char_count};
        break;
      }
    }
  } else if (page < anchor->page) {
    for (int other = page + 1; other <= anchor->page && !cancel.load(); ++other) {
      if (reader.areas(other).has_text()) {
        focus = PdfTextCaret{other, 0};
        break;
      }
    }
  }
  if (!focus.has_value() || cancel.load()) {
    return;
  }
  // After a word, a line or whole pages, the selection grows from the end further away.
  PdfTextCaret from = *anchor;
  if (request.anchor_far.has_value() && *focus < *anchor) {
    from = *request.anchor_far;
  }
  add_range(reader, std::min(from, *focus), std::max(from, *focus), previous, cancel, answer.pieces);
  answer.anchor = anchor;
  answer.anchor_far = request.anchor_far;
  answer.applies = !cancel.load();
}

// The word around a caret as [first, last), or nothing: `char_at` gives the text of one
// character (empty for none). A caret between a word and something else belongs to the
// word; a punctuation mark stands by itself; spaces select nothing.
template <typename CharAt>
std::pair<int, int> word_range(int caret, int count, CharAt&& char_at) {
  enum class Kind { None, Word, Mark };
  const auto kind = [&](int index) {
    if (index < 0 || index >= count) {
      return Kind::None;
    }
    const char32_t c = first_code_point(char_at(index));
    if (c == 0) {
      return Kind::None;
    }
    if (is_word_character(c)) {
      return Kind::Word;
    }
    return QChar::isSpace(c) || c < 0x20 || c == 0xFFFE ? Kind::None : Kind::Mark;
  };
  int pivot = -1;
  if (kind(caret) == Kind::Word) {
    pivot = caret;
  } else if (kind(caret - 1) == Kind::Word) {
    pivot = caret - 1;
  }
  if (pivot < 0) {
    if (kind(caret) == Kind::Mark) {
      return {caret, caret + 1};
    }
    if (kind(caret - 1) == Kind::Mark) {
      return {caret - 1, caret};
    }
    return {0, 0};
  }
  int first = pivot;
  while (pivot - first < kWordReach && kind(first - 1) == Kind::Word) {
    --first;
  }
  int last = pivot + 1;
  while (last - pivot < kWordReach && kind(last) == Kind::Word) {
    ++last;
  }
  return {first, last};
}

void answer_word(Reader& reader, const Request& request, Answer& answer) {
  const int page = request.focus.page;
  if (!reader.valid_page(page)) {
    return;
  }
  const PdfPageTextAreas& areas = reader.areas(page);
  QPdfDocument* document = reader.document;
  const int caret = caret_at(document, page, areas, request.focus.point);
  if (caret < 0) {
    return;
  }
  if (areas.recognized != nullptr) {
    // Recognized text is its own string: the word is read straight from it.
    const auto [first, last] = word_range(caret, areas.char_count, [&](int index) {
      const QChar unit = areas.text[index];
      return unit == QLatin1Char('\n') ? QString() : QString(unit);
    });
    answer.applies = true;
    if (auto piece = last > first ? make_piece(reader, page, first, last - first, {}) : nullptr; piece != nullptr) {
      answer.anchor = PdfTextCaret{page, piece->start};
      answer.anchor_far = PdfTextCaret{page, piece->start + piece->count};
      answer.pieces.push_back(std::move(piece));
    }
    return;
  }
  // The characters around the caret in one call. Its text counts in UTF-16 units and Qt
  // PDF's indices in PDFium characters; the two agree unless a character near by lies
  // outside the BMP or PDFium's text leaves one out. The selection made from the answer
  // says whether they did; when not, the word is followed a character at a time.
  const int window_start = std::max(0, caret - kWordWindow);
  const int window_count = std::min(areas.char_count - window_start, 2 * kWordWindow);
  PiecePtr piece;
  if (window_count > 0) {
    const QString window = document->getSelectionAtIndex(page, window_start, window_count).text();
    if (window.size() == window_count) {
      const auto [first, last] = word_range(caret, areas.char_count, [&](int index) {
        const int offset = index - window_start;
        return offset >= 0 && offset < window.size() ? QString(window[offset]) : QString();
      });
      if (last > first) {
        const QPdfSelection selection = document->getSelectionAtIndex(page, first, last - first);
        if (selection.text() == window.mid(first - window_start, last - first)) {
          piece = piece_from(page, first, last - first, selection);
        }
      } else {
        answer.applies = true;  // a space: nothing is selected
        return;
      }
    }
  }
  if (piece == nullptr) {
    const auto [first, last] = word_range(caret, areas.char_count, [&](int index) {
      return document->getSelectionAtIndex(page, index, 1).text();
    });
    if (last > first) {
      piece = piece_from(page, first, last - first, document->getSelectionAtIndex(page, first, last - first));
    }
  }
  answer.applies = true;
  if (piece != nullptr) {
    answer.anchor = PdfTextCaret{page, piece->start};
    answer.anchor_far = PdfTextCaret{page, piece->start + piece->count};
    answer.pieces.push_back(std::move(piece));
  }
}

// Two line pieces of one line: they share most of their height.
bool same_line(const QRectF& a, const QRectF& b) {
  const double overlap = std::min(a.bottom(), b.bottom()) - std::max(a.top(), b.top());
  return overlap > 0.5 * std::min(a.height(), b.height());
}

void answer_line(Reader& reader, const Request& request, Answer& answer) {
  const int page = request.focus.page;
  if (!reader.valid_page(page)) {
    return;
  }
  const PdfPageTextAreas& areas = reader.areas(page);
  if (areas.has_text() && areas.recognized != nullptr) {
    // A recognized line is one run of the page's text.
    const auto [start, count] = areas.recognized->line_range_at(request.focus.point);
    answer.applies = true;
    if (auto piece = make_piece(reader, page, start, count, {}); piece != nullptr) {
      answer.anchor = PdfTextCaret{page, piece->start};
      answer.anchor_far = PdfTextCaret{page, piece->start + piece->count};
      answer.pieces.push_back(std::move(piece));
    }
    return;
  }
  const int hit = nearest_piece(areas, request.focus.point);
  if (!areas.has_text() || hit < 0) {
    return;
  }
  // The line is the run of pieces around the one clicked that share its row.
  int first = hit;
  while (first > 0 && same_line(areas.pieces[first - 1], areas.pieces[first])) {
    --first;
  }
  int last = hit;
  while (last + 1 < areas.pieces.size() && same_line(areas.pieces[last], areas.pieces[last + 1])) {
    ++last;
  }
  QRectF left = areas.pieces[first];
  QRectF right = areas.pieces[first];
  for (int index = first; index <= last; ++index) {
    const QRectF& piece = areas.pieces[index];
    if (piece.left() < left.left()) {
      left = piece;
    }
    if (piece.right() > right.right()) {
      right = piece;
    }
  }
  const int a = caret_at(reader.document, page, areas, QPointF(left.left(), left.center().y()));
  const int b = caret_at(reader.document, page, areas, QPointF(right.right(), right.center().y()));
  if (a < 0 || b < 0) {
    return;
  }
  answer.applies = true;
  if (auto piece = make_piece(reader, page, std::min(a, b), std::abs(b - a), {}); piece != nullptr) {
    answer.anchor = PdfTextCaret{page, piece->start};
    answer.anchor_far = PdfTextCaret{page, piece->start + piece->count};
    answer.pieces.push_back(std::move(piece));
  }
}

void answer_pages(Reader& reader, const Request& request, const std::vector<PiecePtr>& previous,
                  const std::atomic<bool>& cancel, Answer& answer) {
  const int count = static_cast<int>(reader.known.size());
  const int first = std::clamp(request.first_page, 0, std::max(0, count - 1));
  const int last = std::clamp(request.last_page, first, std::max(0, count - 1));
  for (int page = first; page <= last && page < count && !cancel.load(); ++page) {
    if (auto piece = make_piece(reader, page, 0, std::numeric_limits<int>::max(), previous); piece != nullptr) {
      answer.pieces.push_back(std::move(piece));
    }
  }
  if (!answer.pieces.empty()) {
    const auto& front = *answer.pieces.front();
    const auto& back = *answer.pieces.back();
    answer.anchor = PdfTextCaret{front.page, front.start};
    answer.anchor_far = PdfTextCaret{back.page, back.start + back.count};
  }
  answer.applies = !cancel.load();
}

}  // namespace

QString pdf_copied_text(const QString& page_text) {
  QString out;
  out.reserve(page_text.size() + 8);
  const qsizetype size = page_text.size();
  for (qsizetype index = 0; index < size; ++index) {
    const char16_t c = page_text[index].unicode();
    const char16_t next = index + 1 < size ? page_text[index + 1].unicode() : u'\0';
    if (c == u'\r') {
      if (next != u'\n') {
        out += QLatin1Char('\n');
      }
    } else if (c == u'\n') {
      out += QLatin1Char('\n');
    } else if (c == 0xFFFE || c == 0x02) {
      // PDFium's mark for a hyphen that ends a line inside a word: the page shows the
      // hyphen and goes on a new line.
      out += QLatin1Char('-');
      if (index + 1 < size && next != u'\r' && next != u'\n') {
        out += QLatin1Char('\n');
      }
    } else if ((c < 0x20 && c != u'\t') || c == 0x7F || c == 0xFFFF) {
      continue;
    } else {
      out += QChar(c);
    }
  }
  return out;
}

PdfTextSelection::PdfTextSelection(QObject* parent) : QObject(parent) {}

PdfTextSelection::~PdfTextSelection() {
  stop_worker();
}

void PdfTextSelection::stop_worker() {
  if (cancel_ != nullptr) {
    cancel_->store(true);
  }
  if (in_flight_.valid()) {
    in_flight_.wait();
  }
}

void PdfTextSelection::set_document(QPdfDocument* document) {
  stop_worker();
  ++document_generation_;
  ++epoch_;
  document_ = document;
  const int count = document_ != nullptr ? std::max(0, document_->pageCount()) : 0;
  areas_.assign(static_cast<std::size_t>(count), nullptr);
  wanted_.clear();
  pending_.reset();
  anchor_.reset();
  anchor_far_.reset();
  anchor_origin_.reset();
  drop_selection();
}

void PdfTextSelection::request_pages(const std::vector<int>& pages) {
  wanted_.clear();
  for (const int page : pages) {
    if (page >= 0 && page < static_cast<int>(areas_.size()) && areas_[static_cast<std::size_t>(page)] == nullptr &&
        std::find(wanted_.begin(), wanted_.end(), page) == wanted_.end()) {
      wanted_.push_back(page);
    }
  }
  pump();
}

void PdfTextSelection::set_recognized_text(int page, std::shared_ptr<const PdfOcrPageText> recognized) {
  if (page < 0 || page >= static_cast<int>(areas_.size()) || recognized == nullptr) {
    return;
  }
  auto areas = std::make_shared<PdfPageTextAreas>();
  areas->text = recognized->text;
  areas->char_count = static_cast<int>(areas->text.size());
  areas->pieces = recognized->line_boxes();
  if (!areas->pieces.isEmpty()) {
    areas->base_index = 0;
    areas->base_hit = 0;
    areas->base_box = areas->pieces.constFirst();
  }
  areas->recognized = std::move(recognized);
  // A worker's answer about the page as it was (no text) only ever fills an empty slot, so
  // it cannot take this away again.
  areas_[static_cast<std::size_t>(page)] = std::move(areas);
  std::erase(wanted_, page);
  emit page_areas_ready(page);
}

const PdfPageTextAreas* PdfTextSelection::page_areas(int page) const {
  if (page < 0 || page >= static_cast<int>(areas_.size())) {
    return nullptr;
  }
  return areas_[static_cast<std::size_t>(page)].get();
}

bool PdfTextSelection::text_at(int page, QPointF point, double slack) const {
  const PdfPageTextAreas* areas = page_areas(page);
  if (areas == nullptr || !areas->has_text()) {
    return false;
  }
  for (const QRectF& piece : areas->pieces) {
    // The gap belongs to the line along its length; text set down the page has its lines
    // side by side.
    const bool down_the_page = piece.height() > 2.0 * piece.width();
    const double across = slack + (down_the_page ? kLineGapShare * piece.width() : 0.0);
    const double along = slack + (down_the_page ? 0.0 : kLineGapShare * piece.height());
    if (piece.adjusted(-across, -along, across, along).contains(point)) {
      return true;
    }
  }
  return false;
}

void PdfTextSelection::drop_selection() {
  if (pieces_.empty()) {
    return;
  }
  pieces_.clear();
  emit selection_changed();
}

void PdfTextSelection::set_anchor(int page, QPointF point) {
  ++epoch_;
  pending_.reset();
  anchor_.reset();
  anchor_far_.reset();
  anchor_origin_ = PagePoint{page, point};
  drop_selection();
}

void PdfTextSelection::select_to(int page, QPointF point) {
  if (!has_anchor()) {
    return;
  }
  Request request;
  request.kind = Request::Kind::Range;
  request.anchor = anchor_;
  request.anchor_far = anchor_far_;
  if (anchor_origin_.has_value()) {
    request.anchor_origin = *anchor_origin_;
  }
  request.focus = PagePoint{page, point};
  submit(std::move(request));
}

void PdfTextSelection::select_word(int page, QPointF point) {
  ++epoch_;
  anchor_.reset();
  anchor_far_.reset();
  anchor_origin_ = PagePoint{page, point};
  Request request;
  request.kind = Request::Kind::Word;
  request.focus = PagePoint{page, point};
  submit(std::move(request));
}

void PdfTextSelection::select_line(int page, QPointF point) {
  ++epoch_;
  anchor_.reset();
  anchor_far_.reset();
  anchor_origin_ = PagePoint{page, point};
  Request request;
  request.kind = Request::Kind::Line;
  request.focus = PagePoint{page, point};
  submit(std::move(request));
}

void PdfTextSelection::select_pages(int first, int last) {
  ++epoch_;
  anchor_.reset();
  anchor_far_.reset();
  anchor_origin_.reset();
  Request request;
  request.kind = Request::Kind::Pages;
  request.first_page = std::min(first, last);
  request.last_page = std::max(first, last);
  submit(std::move(request));
}

void PdfTextSelection::clear() {
  ++epoch_;
  pending_.reset();
  anchor_.reset();
  anchor_far_.reset();
  anchor_origin_.reset();
  drop_selection();
}

QString PdfTextSelection::text() const {
  QString out;
  for (const auto& piece : pieces_) {
    const QString part = pdf_copied_text(piece->text);
    if (part.isEmpty()) {
      continue;
    }
    if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n'))) {
      out += QLatin1Char('\n');
    }
    out += part;
  }
  return out;
}

const QList<QRectF>* PdfTextSelection::page_rects(int page) const {
  for (const auto& piece : pieces_) {
    if (piece->page == page) {
      return &piece->rects;
    }
  }
  return nullptr;
}

bool PdfTextSelection::busy() const {
  return busy_ || pending_.has_value() || !wanted_.empty();
}

void PdfTextSelection::submit(Request request) {
  if (document_ == nullptr) {
    return;
  }
  request.epoch = epoch_;
  // The newest question replaces one that is still waiting: a drag only ever wants the
  // answer for where the pointer is now.
  pending_ = std::move(request);
  pump();
}

void PdfTextSelection::pump() {
  if (busy_ || document_ == nullptr) {
    return;
  }
  std::optional<Request> request = std::move(pending_);
  pending_.reset();
  std::vector<int> wanted;
  if (!request.has_value()) {
    std::erase_if(wanted_, [this](int page) { return areas_[static_cast<std::size_t>(page)] != nullptr; });
    if (wanted_.empty()) {
      return;
    }
    wanted = wanted_;
  }
  busy_ = true;
  cancel_ = std::make_shared<std::atomic<bool>>(false);
  QPdfDocument* document = document_;
  const std::uint64_t generation = document_generation_;
  in_flight_ = launch_async([this, document, request = std::move(request), wanted = std::move(wanted),
                             known = areas_, previous = pieces_, cancel = cancel_, generation]() mutable {
    Answer answer;
    Reader reader;
    reader.document = document;
    reader.known = std::move(known);
    try {
      if (request.has_value()) {
        switch (request->kind) {
          case Request::Kind::Range:
            answer_range(reader, *request, previous, *cancel, answer);
            break;
          case Request::Kind::Word:
            answer_word(reader, *request, answer);
            break;
          case Request::Kind::Line:
            answer_line(reader, *request, answer);
            break;
          case Request::Kind::Pages:
            answer_pages(reader, *request, previous, *cancel, answer);
            break;
        }
      } else {
        QElapsedTimer timer;
        timer.start();
        for (const int page : wanted) {
          reader.areas(page);
          if (timer.elapsed() >= kSliceMs || cancel->load()) {
            break;
          }
        }
      }
    } catch (...) {
      answer.applies = false;
      answer.pieces.clear();
    }
    answer.read = std::move(reader.read);
    const bool asked = request.has_value();
    const std::uint64_t epoch = asked ? request->epoch : 0;
    QMetaObject::invokeMethod(
        this,
        [this, answer = std::move(answer), asked, epoch, generation]() mutable {
          busy_ = false;
          if (generation != document_generation_) {
            pump();
            return;
          }
          if (!asked && answer.read.empty() && !wanted_.empty()) {
            // The read failed outright: give the page up as one without text, or it would
            // be asked for forever.
            answer.read.emplace_back(wanted_.front(), std::make_shared<PdfPageTextAreas>());
          }
          std::vector<int> ready;
          for (auto& [page, areas] : answer.read) {
            auto& slot = areas_[static_cast<std::size_t>(page)];
            if (slot == nullptr) {
              slot = std::move(areas);
              ready.push_back(page);
            }
            std::erase(wanted_, page);
          }
          for (const int page : ready) {
            emit page_areas_ready(page);
          }
          if (asked && epoch == epoch_ && answer.applies) {
            anchor_ = answer.anchor;
            anchor_far_ = answer.anchor_far;
            pieces_ = std::move(answer.pieces);
            emit selection_changed();
          }
          pump();
        },
        Qt::QueuedConnection);
  });
}

}  // namespace patchy::ui
