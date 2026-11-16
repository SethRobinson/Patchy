#pragma once

#include <QList>
#include <QObject>
#include <QRectF>
#include <QString>

#include <cstdint>
#include <future>
#include <memory>
#include <vector>

class QPdfDocument;

namespace patchy::ui {

struct PdfPageText;

// Find in the PDF viewer (docs/pdf-viewer.md, "Find").

struct PdfFindOptions {
  bool match_case{false};
  bool whole_words{false};
  [[nodiscard]] bool operator==(const PdfFindOptions&) const = default;
};

// A match in a page's text: UTF-16 offsets into the text as Qt PDF hands it out.
struct PdfTextMatch {
  int start{0};
  int length{0};
  [[nodiscard]] bool operator==(const PdfTextMatch&) const = default;
};

// A page's text made comparable (prepare_pdf_search_text), with the way back to the source.
struct PdfSearchText {
  QString text;
  // Per UTF-16 unit of `text`: the source offset it came from and how many source units it
  // stands for (a run of spaces and line breaks is one space).
  std::vector<std::uint32_t> start;
  std::vector<std::uint16_t> span;
  // Offsets into `text` where a hyphen at a line end was dropped with its line break, so the
  // two halves of the word are joined.
  std::vector<std::uint32_t> joins;
};

// The comparable form of `source`:
// - every run of spaces, tabs and line breaks is one space, so a phrase matches across lines;
// - a hyphen that ends a line between two letters or digits goes, with the break, so
//   "exam-<newline>ple" reads "example" (find_pdf_text_matches still finds "well-known"
//   there);
// - compatibility forms are decomposed (the fi ligature, full-width letters, accents as
//   base letter plus mark), curly quotes become straight ones, the dashes a hyphen;
// - soft hyphens and zero-width characters go.
// Case is kept: the search folds it.
[[nodiscard]] PdfSearchText prepare_pdf_search_text(const QString& source);

// What a typed query is compared as: the same form, empty when nothing but spaces was typed.
[[nodiscard]] QString pdf_search_needle(const QString& query);

// Every match of `needle` (from pdf_search_needle) in `page`, in reading order, never
// overlapping. Case is folded the Unicode way (simple folding, every script) unless
// options.match_case; options.whole_words wants no letter, digit or mark touching an end of
// the match that is itself a letter or digit.
[[nodiscard]] std::vector<PdfTextMatch> find_pdf_text_matches(const PdfSearchText& page, const QString& needle,
                                                              PdfFindOptions options);
// The same from plain strings.
[[nodiscard]] std::vector<PdfTextMatch> find_pdf_text_matches(const QString& text, const QString& query,
                                                              PdfFindOptions options);

// The search of one open PDF: reads each page's text once, on a worker and in short slices
// (PDFium serializes every call, and the page renders share that lock), matches the query
// there, and tells the window and the page view what it has as it comes in. Where a match
// sits on its page is worked out only for the current match and the pages on screen.
//
// The document is not owned and must outlive this object, or be detached first with
// set_document(nullptr).
class PdfTextSearch final : public QObject {
  Q_OBJECT

public:
  enum class TextLayer { Unknown, Present, Absent };
  struct MatchRef {
    int page{-1};
    int index{-1};
    [[nodiscard]] bool valid() const { return page >= 0 && index >= 0; }
    [[nodiscard]] bool operator==(const MatchRef&) const = default;
  };
  struct PageMatches {
    std::vector<PdfTextMatch> matches;
    // Per match: where it is, in points from the top left of the page as stored (before the
    // view's rotation), a rectangle per line piece. Empty until worked out.
    std::vector<QList<QRectF>> rects;
  };

  explicit PdfTextSearch(QObject* parent = nullptr);
  ~PdfTextSearch() override;

  void set_document(QPdfDocument* document);

  // Searches for `query`. The current match becomes the first one at or after the current
  // match, or after the top of `from_page` when there is none, wrapping past the last page.
  void set_query(const QString& query, PdfFindOptions options, int from_page);
  void clear();
  [[nodiscard]] bool active() const { return !needle_.isEmpty(); }
  [[nodiscard]] PdfFindOptions options() const { return options_; }

  // Matches found so far, the current one's place among them in document order (-1: none).
  [[nodiscard]] int match_count() const { return match_count_; }
  [[nodiscard]] int current_index() const;
  [[nodiscard]] MatchRef current_match() const { return current_; }
  // True while pages are still to be searched.
  [[nodiscard]] bool searching() const { return active() && unmatched_pages_ > 0; }
  // Absent once every page was read and none holds text (a scan without a text layer).
  [[nodiscard]] TextLayer text_layer() const { return text_layer_; }

  // Steps to the next or previous match, around the ends of the document (`wrapped` says
  // so). A step asked for before the search has a current match (Enter typed right behind
  // the text) is kept and taken from the first match once it is known, so the key lands
  // where it would have a moment later. False when there is nothing to step to.
  bool find_next();
  bool find_previous();

  // The page view's side: the pages on screen get their matches placed; a page's matches
  // for painting (nullptr when it has none).
  void set_visible_pages(const std::vector<int>& pages);
  [[nodiscard]] const PageMatches* page_matches(int page) const;

  // Tests: true while a search or a placement is queued or running.
  [[nodiscard]] bool busy() const;
  // Tests: the text read for `page` (empty until read).
  [[nodiscard]] QString page_text_for_testing(int page) const;

signals:
  void results_changed();
  void highlights_changed();
  void current_match_changed();
  // The current match's rectangles are known.
  void current_match_located();
  void wrapped(bool forward);

private:
  struct PageState {
    bool matched{false};
    PageMatches found;
    std::vector<std::uint8_t> located;
    int located_count{0};
  };

  [[nodiscard]] int page_count() const { return static_cast<int>(pages_.size()); }
  void stop_worker();
  void pump();
  [[nodiscard]] int geometry_page() const;
  void start_match_task();
  void start_geometry_task(int page);
  void choose_current();
  void set_current(MatchRef match);
  bool step(int direction);

  QPdfDocument* document_{nullptr};
  std::vector<std::shared_ptr<const PdfPageText>> texts_;
  int texts_read_{0};
  TextLayer text_layer_{TextLayer::Unknown};

  QString needle_;
  PdfFindOptions options_;
  std::vector<PageState> pages_;
  int unmatched_pages_{0};
  int match_count_{0};
  MatchRef current_;
  int anchor_page_{0};
  int anchor_offset_{0};
  std::vector<int> visible_pages_;

  std::future<void> in_flight_;
  bool busy_{false};
  bool last_task_placed_{false};
  // Steps asked for while no current match was known yet: forward positive, back negative.
  int pending_steps_{0};
  std::uint64_t document_generation_{0};
  std::uint64_t query_generation_{0};
};

}  // namespace patchy::ui
