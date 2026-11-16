#include "ui/pdf_text_search.hpp"

#include "ui/background_workers.hpp"

#include <QChar>
#include <QElapsedTimer>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPolygonF>
#include <QStringView>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

namespace patchy::ui {

namespace {

// A worker task gives the PDFium lock back after about this long, so a page render never
// waits behind a long search.
constexpr qint64 kSliceMs = 12;
constexpr std::size_t kPagesPerTask = 256;
constexpr std::size_t kMatchesPerTask = 512;
// How far past its text offset a match's first character is looked for when PDFium counts
// characters its text leaves out (see locate_match).
constexpr int kIndexProbes = 12;
// A held Enter during a long search queues no more steps than this.
constexpr int kMaxPendingSteps = 32;

char32_t code_point_at(QStringView text, qsizetype index, int* length) {
  const char16_t unit = text[index].unicode();
  if (QChar::isHighSurrogate(unit) && index + 1 < text.size() && QChar::isLowSurrogate(text[index + 1].unicode())) {
    *length = 2;
    return QChar::surrogateToUcs4(unit, text[index + 1].unicode());
  }
  *length = 1;
  return unit;
}

// The code point that ends just before `index` (0 at the start).
char32_t code_point_before(QStringView text, qsizetype index) {
  if (index <= 0) {
    return 0;
  }
  const char16_t unit = text[index - 1].unicode();
  if (QChar::isLowSurrogate(unit) && index >= 2 && QChar::isHighSurrogate(text[index - 2].unicode())) {
    return QChar::surrogateToUcs4(text[index - 2].unicode(), unit);
  }
  return unit;
}

bool is_letter_digit_or_mark(char32_t c) {
  return QChar::isLetterOrNumber(c) || QChar::isMark(c);
}

bool is_word_character(char32_t c) {
  return is_letter_digit_or_mark(c) || c == U'_';
}

bool is_line_break(char32_t c) {
  return c == U'\n' || c == U'\r' || c == 0x0B || c == 0x0C || c == 0x85 || c == 0x2028 || c == 0x2029;
}

// A hyphen that may end a line inside a word: the typed ones, the soft hyphen, and the two
// marks PDFium leaves where it found such a hyphen itself (U+0002, U+FFFE).
bool is_breaking_hyphen(char32_t c) {
  return c == U'-' || c == 0x2010 || c == 0x2011 || c == 0xAD || c == 0x02 || c == 0xFFFE;
}

// Characters that are nothing to a reader: the soft hyphen inside a line, zero-width
// spaces and joiners, the byte order mark, control characters.
bool is_ignorable(char32_t c) {
  return c == 0xAD || c == 0x200B || c == 0x200C || c == 0x200D || c == 0x2060 || c == 0xFEFF || c < 0x20 ||
         c == 0x7F;
}

// Quotes and dashes as the keyboard types them; 0 for anything else.
char16_t typed_form(char32_t c) {
  switch (c) {
    case 0x2018:
    case 0x2019:
    case 0x201A:
    case 0x201B:
    case 0x2032:
      return u'\'';
    case 0x201C:
    case 0x201D:
    case 0x201E:
    case 0x201F:
    case 0x2033:
      return u'"';
    case 0x02:
    case 0xFFFE:
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2014:
    case 0x2015:
    case 0x2212:
      return u'-';
    default:
      return 0;
  }
}

bool matches_at(QStringView haystack, qsizetype at, QStringView needle, Qt::CaseSensitivity sensitivity) {
  return at >= 0 && at + needle.size() <= haystack.size() &&
         haystack.mid(at, needle.size()).compare(needle, sensitivity) == 0;
}

// Whether [begin, end) of `text` may stand as a match of `needle`.
bool acceptable_match(QStringView text, qsizetype begin, qsizetype end, QStringView needle, bool whole_words) {
  int length = 1;
  // Never half a letter: a match must not stop before the accent of its last letter.
  if (end < text.size() && QChar::isMark(code_point_at(text, end, &length))) {
    return false;
  }
  if (!whole_words) {
    return true;
  }
  if (is_word_character(code_point_at(needle, 0, &length)) && is_word_character(code_point_before(text, begin))) {
    return false;
  }
  return !(is_word_character(code_point_before(needle, needle.size())) && end < text.size() &&
           is_word_character(code_point_at(text, end, &length)));
}

}  // namespace

PdfSearchText prepare_pdf_search_text(const QString& source) {
  PdfSearchText out;
  const QStringView view(source);
  const qsizetype size = view.size();
  out.text.reserve(size);
  out.start.reserve(static_cast<std::size_t>(size));
  out.span.reserve(static_cast<std::size_t>(size));
  char32_t previous = 0;  // the last code point written
  const auto write = [&out, &previous](QStringView units, qsizetype at, qsizetype length) {
    for (const QChar unit : units) {
      out.text.append(unit);
      out.start.push_back(static_cast<std::uint32_t>(at));
      out.span.push_back(static_cast<std::uint16_t>(
          std::min<qsizetype>(length, std::numeric_limits<std::uint16_t>::max())));
    }
    if (!units.isEmpty()) {
      previous = code_point_before(units, units.size());
    }
  };
  // The end of the run of spaces and line breaks starting at `from`, and whether it holds a
  // line break.
  const auto space_run = [&view, size](qsizetype from, bool* line_break) {
    qsizetype end = from;
    while (end < size) {
      int length = 1;
      const char32_t c = code_point_at(view, end, &length);
      if (!QChar::isSpace(c)) {
        break;
      }
      *line_break = *line_break || is_line_break(c);
      end += length;
    }
    return end;
  };
  qsizetype index = 0;
  while (index < size) {
    int length = 1;
    const char32_t c = code_point_at(view, index, &length);
    if (QChar::isSpace(c)) {
      bool line_break = false;
      const qsizetype end = space_run(index, &line_break);
      write(u" ", index, end - index);
      index = end;
      continue;
    }
    if (is_breaking_hyphen(c) && is_letter_digit_or_mark(previous)) {
      bool line_break = false;
      const qsizetype end = space_run(index + length, &line_break);
      int next_length = 1;
      // PDFium's marks stand only where it joined two lines itself; it may keep the break or not.
      const bool pdfium_mark = c == 0x02 || c == 0xFFFE;
      if ((line_break || pdfium_mark) && end < size &&
          is_letter_digit_or_mark(code_point_at(view, end, &next_length))) {
        out.joins.push_back(static_cast<std::uint32_t>(out.text.size()));
        index = end;
        continue;
      }
    }
    if (const char16_t typed = typed_form(c); typed != 0) {
      write(QStringView(&typed, 1), index, length);
    } else if (is_ignorable(c)) {
      // Dropped.
    } else if (c < 0x80 || QChar::decompositionTag(c) == QChar::NoDecomposition) {
      write(view.mid(index, length), index, length);
    } else {
      write(QString::fromUcs4(&c, 1).normalized(QString::NormalizationForm_KD), index, length);
    }
    index += length;
  }
  return out;
}

QString pdf_search_needle(const QString& query) {
  QString needle = prepare_pdf_search_text(query).text;
  if (needle.trimmed().isEmpty()) {
    needle.clear();
  }
  return needle;
}

std::vector<PdfTextMatch> find_pdf_text_matches(const PdfSearchText& page, const QString& needle,
                                                PdfFindOptions options) {
  std::vector<PdfTextMatch> matches;
  const QStringView text(page.text);
  const QStringView wanted(needle);
  if (wanted.isEmpty() || text.isEmpty()) {
    return matches;
  }
  const Qt::CaseSensitivity sensitivity = options.match_case ? Qt::CaseSensitive : Qt::CaseInsensitive;
  const auto add = [&](qsizetype begin, qsizetype end) {
    const auto last = static_cast<std::size_t>(end - 1);
    const int start = static_cast<int>(page.start[static_cast<std::size_t>(begin)]);
    const int stop = static_cast<int>(page.start[last]) + page.span[last];
    if (stop > start) {
      matches.push_back({start, stop - start});
    }
  };
  qsizetype from = 0;
  while (from <= text.size() - wanted.size()) {
    const qsizetype at = text.indexOf(wanted, from, sensitivity);
    if (at < 0) {
      break;
    }
    if (acceptable_match(text, at, at + wanted.size(), wanted, options.whole_words)) {
      add(at, at + wanted.size());
      from = at + wanted.size();
    } else {
      from = at + 1;
    }
  }
  // A word hyphenated at a line end lost its hyphen, so a query that types one ("well-known")
  // is also tried without it across each such join.
  if (!page.joins.empty() && wanted.size() >= 3) {
    const std::size_t plain = matches.size();
    for (qsizetype hyphen = 1; hyphen + 1 < wanted.size(); ++hyphen) {
      if (wanted[hyphen] != u'-') {
        continue;
      }
      const QString joined = needle.left(hyphen) + needle.mid(hyphen + 1);
      for (const std::uint32_t join : page.joins) {
        const qsizetype at = static_cast<qsizetype>(join) - hyphen;
        if (matches_at(text, at, joined, sensitivity) &&
            acceptable_match(text, at, at + joined.size(), joined, options.whole_words)) {
          add(at, at + joined.size());
        }
      }
    }
    if (matches.size() > plain) {
      std::sort(matches.begin(), matches.end(), [](const PdfTextMatch& a, const PdfTextMatch& b) {
        return a.start != b.start ? a.start < b.start : a.length > b.length;
      });
      std::vector<PdfTextMatch> kept;
      kept.reserve(matches.size());
      for (const auto& match : matches) {
        if (kept.empty() || match.start >= kept.back().start + kept.back().length) {
          kept.push_back(match);
        }
      }
      matches = std::move(kept);
    }
  }
  return matches;
}

std::vector<PdfTextMatch> find_pdf_text_matches(const QString& text, const QString& query, PdfFindOptions options) {
  return find_pdf_text_matches(prepare_pdf_search_text(text), pdf_search_needle(query), options);
}

// One page's text as read, shared with the worker tasks that search and place it.
struct PdfPageText {
  QString text;
  // PDFium's character count for the page: what Qt PDF's indices count in.
  int char_count{0};
  PdfSearchText search;
  bool has_text{false};
};

namespace {

using PageTextPtr = std::shared_ptr<const PdfPageText>;

}  // namespace

PdfTextSearch::PdfTextSearch(QObject* parent) : QObject(parent) {}

PdfTextSearch::~PdfTextSearch() {
  stop_worker();
}

void PdfTextSearch::stop_worker() {
  if (in_flight_.valid()) {
    in_flight_.wait();
  }
}

void PdfTextSearch::set_document(QPdfDocument* document) {
  stop_worker();
  ++document_generation_;
  ++query_generation_;
  document_ = document;
  const int count = document_ != nullptr ? std::max(0, document_->pageCount()) : 0;
  texts_.assign(static_cast<std::size_t>(count), nullptr);
  texts_read_ = 0;
  text_layer_ = TextLayer::Unknown;
  pages_.assign(static_cast<std::size_t>(count), PageState{});
  unmatched_pages_ = needle_.isEmpty() ? 0 : count;
  match_count_ = 0;
  current_ = {};
  pending_steps_ = 0;
  anchor_page_ = 0;
  anchor_offset_ = 0;
  visible_pages_.clear();
  emit results_changed();
  emit highlights_changed();
  pump();
}

void PdfTextSearch::set_query(const QString& query, PdfFindOptions options, int from_page) {
  const QString needle = pdf_search_needle(query);
  if (needle == needle_ && options == options_) {
    return;
  }
  ++query_generation_;
  // Typing on keeps the place: the match being looked at stays current while it still fits.
  if (current_.valid()) {
    anchor_page_ = current_.page;
    anchor_offset_ = pages_[static_cast<std::size_t>(current_.page)]
                         .found.matches[static_cast<std::size_t>(current_.index)]
                         .start;
  } else {
    anchor_page_ = std::clamp(from_page, 0, std::max(0, page_count() - 1));
    anchor_offset_ = 0;
  }
  needle_ = needle;
  options_ = options;
  for (auto& state : pages_) {
    state = PageState{};
  }
  unmatched_pages_ = needle_.isEmpty() ? 0 : page_count();
  match_count_ = 0;
  current_ = {};
  pending_steps_ = 0;
  emit results_changed();
  emit highlights_changed();
  pump();
}

void PdfTextSearch::clear() {
  set_query(QString(), options_, anchor_page_);
}

int PdfTextSearch::current_index() const {
  if (!current_.valid()) {
    return -1;
  }
  int index = current_.index;
  for (int page = 0; page < current_.page; ++page) {
    index += static_cast<int>(pages_[static_cast<std::size_t>(page)].found.matches.size());
  }
  return index;
}

const PdfTextSearch::PageMatches* PdfTextSearch::page_matches(int page) const {
  if (page < 0 || page >= page_count()) {
    return nullptr;
  }
  const auto& found = pages_[static_cast<std::size_t>(page)].found;
  return found.matches.empty() ? nullptr : &found;
}

void PdfTextSearch::set_visible_pages(const std::vector<int>& pages) {
  if (pages == visible_pages_) {
    return;
  }
  visible_pages_ = pages;
  pump();
}

bool PdfTextSearch::busy() const {
  return busy_ || (document_ != nullptr && active() && (unmatched_pages_ > 0 || geometry_page() >= 0));
}

QString PdfTextSearch::page_text_for_testing(int page) const {
  if (page < 0 || page >= page_count() || texts_[static_cast<std::size_t>(page)] == nullptr) {
    return {};
  }
  return texts_[static_cast<std::size_t>(page)]->text;
}

// The page whose matches are placed next: the current match's first, then the pages on
// screen. -1 when nothing waits.
int PdfTextSearch::geometry_page() const {
  if (current_.valid()) {
    const auto& state = pages_[static_cast<std::size_t>(current_.page)];
    if (state.located[static_cast<std::size_t>(current_.index)] == 0) {
      return current_.page;
    }
  }
  for (const int page : visible_pages_) {
    if (page < 0 || page >= page_count()) {
      continue;
    }
    const auto& state = pages_[static_cast<std::size_t>(page)];
    if (state.located_count < static_cast<int>(state.found.matches.size())) {
      return page;
    }
  }
  return -1;
}

void PdfTextSearch::pump() {
  if (busy_ || document_ == nullptr || !active()) {
    return;
  }
  const int place = geometry_page();
  const bool search = unmatched_pages_ > 0;
  // Both wanted: take turns, so a page with a thousand matches to place does not hold up
  // the count and a long document does not hold up the highlights.
  if (place >= 0 && (!search || !last_task_placed_)) {
    last_task_placed_ = true;
    start_geometry_task(place);
  } else if (search) {
    last_task_placed_ = false;
    start_match_task();
  }
}

void PdfTextSearch::start_match_task() {
  // Pages from the anchor on, around the end: the nearest results come first.
  std::vector<std::pair<int, PageTextPtr>> wanted;
  const int count = page_count();
  for (int step = 0; step < count && wanted.size() < kPagesPerTask; ++step) {
    const int page = (anchor_page_ + step) % count;
    if (!pages_[static_cast<std::size_t>(page)].matched) {
      wanted.emplace_back(page, texts_[static_cast<std::size_t>(page)]);
    }
  }
  if (wanted.empty()) {
    return;
  }
  busy_ = true;
  QPdfDocument* document = document_;
  const std::uint64_t document_generation = document_generation_;
  const std::uint64_t query_generation = query_generation_;
  in_flight_ = launch_async([this, document, wanted = std::move(wanted), needle = needle_, options = options_,
                             document_generation, query_generation] {
    struct Matched {
      int page{0};
      PageTextPtr text;
      std::vector<PdfTextMatch> matches;
    };
    std::vector<Matched> done;
    try {
      QElapsedTimer timer;
      timer.start();
      for (const auto& [page, known] : wanted) {
        Matched matched;
        matched.page = page;
        matched.text = known;
        if (matched.text == nullptr) {
          auto text = std::make_shared<PdfPageText>();
          const QPdfSelection all = document->getAllText(page);
          text->text = all.text();
          text->char_count = text->text.isEmpty() ? 0 : std::max(0, all.endIndex());
          text->search = prepare_pdf_search_text(text->text);
          text->has_text = !text->search.text.trimmed().isEmpty();
          matched.text = std::move(text);
        }
        matched.matches = find_pdf_text_matches(matched.text->search, needle, options);
        done.push_back(std::move(matched));
        if (timer.elapsed() >= kSliceMs) {
          break;
        }
      }
    } catch (...) {
      // What was read stands; the rest is asked for again.
    }
    QMetaObject::invokeMethod(
        this,
        [this, done = std::move(done), document_generation, query_generation]() mutable {
          busy_ = false;
          if (document_generation != document_generation_) {
            pump();
            return;
          }
          bool found = false;
          for (auto& matched : done) {
            const auto slot = static_cast<std::size_t>(matched.page);
            if (texts_[slot] == nullptr) {
              texts_[slot] = matched.text;
              ++texts_read_;
              if (matched.text->has_text) {
                text_layer_ = TextLayer::Present;
              }
            }
            if (query_generation != query_generation_ || pages_[slot].matched) {
              continue;
            }
            auto& state = pages_[slot];
            state.matched = true;
            --unmatched_pages_;
            match_count_ += static_cast<int>(matched.matches.size());
            found = found || !matched.matches.empty();
            state.found.rects.assign(matched.matches.size(), {});
            state.located.assign(matched.matches.size(), 0);
            state.found.matches = std::move(matched.matches);
          }
          if (texts_read_ == page_count() && text_layer_ == TextLayer::Unknown) {
            text_layer_ = TextLayer::Absent;
          }
          if (done.empty() && query_generation == query_generation_ && unmatched_pages_ > 0) {
            // The worker read nothing (PDFium threw on the first page): give that page up
            // as empty, or the search would ask for it for ever.
            const int count = page_count();
            for (int step = 0; step < count; ++step) {
              auto& state = pages_[static_cast<std::size_t>((anchor_page_ + step) % count)];
              if (!state.matched) {
                state.matched = true;
                --unmatched_pages_;
                break;
              }
            }
          }
          if (query_generation == query_generation_) {
            choose_current();
            if (unmatched_pages_ == 0 && !current_.valid()) {
              pending_steps_ = 0;  // nothing was found: the steps that waited go nowhere
            }
            emit results_changed();
            if (found) {
              emit highlights_changed();
            }
          }
          pump();
        },
        Qt::QueuedConnection);
  });
}

namespace {

// The rectangles of text[start, start + length) on `page`. Qt PDF's indices count PDFium's
// characters, which are the text's UTF-16 units except where PDFium counts a character its
// text leaves out (then the match sits a little further on: probed) or hands a character
// outside the BMP out as two units (then code points are counted). The selection's own
// text says whether an index was right; no rectangles when none is.
QList<QRectF> locate_match(QPdfDocument* document, int page, const PdfPageText& text, const PdfTextMatch& match);

}  // namespace

void PdfTextSearch::start_geometry_task(int page) {
  const auto& state = pages_[static_cast<std::size_t>(page)];
  const PageTextPtr text = texts_[static_cast<std::size_t>(page)];
  std::vector<std::pair<int, PdfTextMatch>> wanted;
  const auto want = [&](int index) {
    if (state.located[static_cast<std::size_t>(index)] == 0 && wanted.size() < kMatchesPerTask) {
      wanted.emplace_back(index, state.found.matches[static_cast<std::size_t>(index)]);
    }
  };
  if (current_.valid() && current_.page == page) {
    want(current_.index);
  }
  for (int index = 0; index < static_cast<int>(state.found.matches.size()); ++index) {
    if (!(current_.valid() && current_.page == page && current_.index == index)) {
      want(index);
    }
  }
  if (wanted.empty() || text == nullptr) {
    return;
  }
  busy_ = true;
  QPdfDocument* document = document_;
  const std::uint64_t document_generation = document_generation_;
  const std::uint64_t query_generation = query_generation_;
  in_flight_ = launch_async(
      [this, document, page, text, wanted = std::move(wanted), document_generation, query_generation] {
        std::vector<std::pair<int, QList<QRectF>>> placed;
        QElapsedTimer timer;
        timer.start();
        for (const auto& [index, match] : wanted) {
          QList<QRectF> rects;
          try {
            rects = locate_match(document, page, *text, match);
          } catch (...) {
            rects.clear();
          }
          placed.emplace_back(index, std::move(rects));
          if (timer.elapsed() >= kSliceMs) {
            break;
          }
        }
        QMetaObject::invokeMethod(
            this,
            [this, page, placed = std::move(placed), document_generation, query_generation]() mutable {
              busy_ = false;
              if (document_generation == document_generation_ && query_generation == query_generation_) {
                auto& state = pages_[static_cast<std::size_t>(page)];
                bool current_placed = false;
                for (auto& [index, rects] : placed) {
                  const auto slot = static_cast<std::size_t>(index);
                  if (slot >= state.located.size() || state.located[slot] != 0) {
                    continue;
                  }
                  state.located[slot] = 1;
                  ++state.located_count;
                  state.found.rects[slot] = std::move(rects);
                  current_placed = current_placed || (current_.page == page && current_.index == index);
                }
                emit highlights_changed();
                if (current_placed) {
                  emit current_match_located();
                }
              }
              pump();
            },
            Qt::QueuedConnection);
      });
}

namespace {

QList<QRectF> locate_match(QPdfDocument* document, int page, const PdfPageText& text, const PdfTextMatch& match) {
  const QStringView source(text.text);
  const QStringView wanted = source.mid(match.start, match.length);
  int first = match.start;
  int count = match.length;
  int probes = std::min(kIndexProbes, std::max(0, text.char_count - static_cast<int>(source.size())));
  if (source.size() > text.char_count) {
    const auto code_points = [](QStringView part) {
      int total = 0;
      for (const QChar unit : part) {
        total += unit.isLowSurrogate() ? 0 : 1;
      }
      return total;
    };
    first = code_points(source.left(match.start));
    count = code_points(wanted);
    probes = 0;
  }
  for (int probe = 0; probe <= probes; ++probe) {
    const QPdfSelection selection = document->getSelectionAtIndex(page, first + probe, count);
    if (QStringView(selection.text()) != wanted) {
      continue;
    }
    QList<QRectF> rects;
    for (const QPolygonF& polygon : selection.bounds()) {
      const QRectF rect = polygon.boundingRect();
      if (rect.width() > 0.0 && rect.height() > 0.0) {
        rects.append(rect);
      }
    }
    return rects;
  }
  return {};
}

}  // namespace

void PdfTextSearch::choose_current() {
  if (current_.valid() || !active()) {
    return;
  }
  // The first match at or after the anchor, pages in order around the end of the document,
  // the anchor's own page a second time for what lies before the anchor. A page not yet
  // searched may hold an earlier one: wait for it.
  const int count = page_count();
  for (int turn = 0; turn <= count && count > 0; ++turn) {
    const int page = (anchor_page_ + turn) % count;
    const auto& state = pages_[static_cast<std::size_t>(page)];
    if (!state.matched) {
      return;
    }
    for (int index = 0; index < static_cast<int>(state.found.matches.size()); ++index) {
      const bool after = state.found.matches[static_cast<std::size_t>(index)].start >= anchor_offset_;
      if ((turn == 0 && !after) || (turn == count && after)) {
        continue;
      }
      set_current({page, index});
      // The steps typed before this was known, taken from it.
      const int steps = std::exchange(pending_steps_, 0);
      for (int taken = 0; taken < std::abs(steps); ++taken) {
        step(steps > 0 ? 1 : -1);
      }
      return;
    }
  }
}

void PdfTextSearch::set_current(MatchRef match) {
  current_ = match;
  emit current_match_changed();
  emit highlights_changed();
  if (pages_[static_cast<std::size_t>(match.page)].located[static_cast<std::size_t>(match.index)] != 0) {
    emit current_match_located();
  }
  pump();
}

bool PdfTextSearch::step(int direction) {
  if (!active()) {
    return false;
  }
  if (!current_.valid()) {
    // Still looking for the first match: the step waits for it (choose_current). A search
    // that has ended without one has nothing to step to.
    if (!searching()) {
      return false;
    }
    pending_steps_ = std::clamp(pending_steps_ + direction, -kMaxPendingSteps, kMaxPendingSteps);
    return true;
  }
  const int count = page_count();
  const auto matches_on = [this](int page) {
    return static_cast<int>(pages_[static_cast<std::size_t>(page)].found.matches.size());
  };
  int page = current_.page;
  int index = current_.index + direction;
  bool around = false;
  if (index < 0 || index >= matches_on(page)) {
    const int origin = page;
    for (int walked = 1; walked <= count; ++walked) {
      page = ((origin + direction * walked) % count + count) % count;
      if (matches_on(page) > 0) {
        around = walked == count || (direction > 0 ? page < origin : page > origin);
        break;
      }
    }
    index = direction > 0 ? 0 : matches_on(page) - 1;
  }
  if (index < 0 || index >= matches_on(page)) {
    return false;
  }
  set_current({page, index});
  emit results_changed();
  if (around) {
    emit wrapped(direction > 0);
  }
  return true;
}

bool PdfTextSearch::find_next() {
  return step(1);
}

bool PdfTextSearch::find_previous() {
  return step(-1);
}

}  // namespace patchy::ui
