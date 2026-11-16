#pragma once

#include <QList>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <atomic>
#include <compare>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <vector>

class QPdfDocument;

namespace patchy::ui {

struct PdfOcrPageText;

// Selecting and copying text in the PDF viewer (docs/pdf-viewer-selection.md).

// Where the text of one page is, read once: its line pieces in points from the top left of
// the page as stored (before the view's rotation), in character order.
struct PdfPageTextAreas {
  // PDFium's character count for the page: what Qt PDF's indices count in.
  int char_count{0};
  QString text;
  QList<QRectF> pieces;
  // The first character that has a box of its own, and the character Qt PDF finds at the
  // middle of that box (the same one, unless another character's box holds that point
  // too). Carets are measured from it (see caret_at in the .cpp).
  int base_index{-1};
  int base_hit{-1};
  QRectF base_box;
  // Set when the page's text is what text recognition read (docs/pdf-viewer-ocr.md): carets
  // and ranges are worked out from it, and Qt PDF is not asked about this page.
  std::shared_ptr<const PdfOcrPageText> recognized;
  [[nodiscard]] bool has_text() const { return base_index >= 0 && !pieces.isEmpty(); }
};

// A place between two characters of a page, counted in PDFium characters.
struct PdfTextCaret {
  int page{-1};
  int index{0};
  [[nodiscard]] auto operator<=>(const PdfTextCaret&) const = default;
};

// What Copy puts on the clipboard for text as PDFium hands it out: line breaks as "\n", a
// word hyphenated at a line end as the page shows it (PDFium's mark for that hyphen becomes
// "-" and a line break), no control characters.
[[nodiscard]] QString pdf_copied_text(const QString& page_text);

// The selection of one open PDF. Every question that needs PDFium (which character is under
// a point, where a range of characters sits) is answered on a worker, like the page renders
// that share PDFium's lock: a drag hands over the newest pointer position and the selection
// follows as answers come in, so a queued run of pointer moves costs nothing.
//
// Points are in points from the top left of the page as stored. The document is not owned
// and must outlive this object, or be detached first with set_document(nullptr).
class PdfTextSelection final : public QObject {
  Q_OBJECT

public:
  explicit PdfTextSelection(QObject* parent = nullptr);
  ~PdfTextSelection() override;

  void set_document(QPdfDocument* document);

  // The pages whose text areas are wanted, most wanted first (the pages on screen).
  void request_pages(const std::vector<int>& pages);
  // A page's text areas; nullptr until they are read.
  [[nodiscard]] const PdfPageTextAreas* page_areas(int page) const;
  // A page that has no text of its own takes the text recognition read from it: its lines
  // become the page's text areas, and it selects and copies like any page.
  void set_recognized_text(int page, std::shared_ptr<const PdfOcrPageText> text);
  // True when `point` is on text of `page`, give or take `slack` points, or in the gap
  // between two lines of a paragraph (a line counts a share of its height above and below
  // it). False while the page's areas are still being read.
  [[nodiscard]] bool text_at(int page, QPointF point, double slack) const;

  // A click: nothing is selected, and the place is where select_to starts from.
  void set_anchor(int page, QPointF point);
  // A drag or Shift+click: everything from the anchor to the character nearest `point`,
  // across pages. Nothing happens without an anchor.
  void select_to(int page, QPointF point);
  [[nodiscard]] bool has_anchor() const { return anchor_.has_value() || anchor_origin_.has_value(); }
  // A double-click and a triple-click.
  void select_word(int page, QPointF point);
  void select_line(int page, QPointF point);
  // All the text of pages first..last.
  void select_pages(int first, int last);
  void clear();

  [[nodiscard]] bool empty() const { return pieces_.empty(); }
  // The selected text as Copy hands it out (pdf_copied_text), pages in order.
  [[nodiscard]] QString text() const;
  // Where the selection is on `page`, a rectangle per line piece; nullptr when it has none.
  [[nodiscard]] const QList<QRectF>* page_rects(int page) const;

  // Tests: true while a question is queued or being answered.
  [[nodiscard]] bool busy() const;

signals:
  void selection_changed();
  void page_areas_ready(int page);

public:
  // The selected part of one page. (Public for the worker's free functions.)
  struct Piece {
    int page{0};
    int start{0};
    int count{0};
    QString text;
    QList<QRectF> rects;
  };
  using PiecePtr = std::shared_ptr<const Piece>;
  using AreasPtr = std::shared_ptr<const PdfPageTextAreas>;
  struct PagePoint {
    int page{-1};
    QPointF point;
  };
  struct Request {
    enum class Kind { Range, Word, Line, Pages };
    Kind kind{Kind::Range};
    // Range: from the anchor (a caret once it is known, else the point pressed) to `focus`.
    // After a word, a line or whole pages were selected the anchor is the end further from
    // the focus (`anchor_far` is the other end).
    std::optional<PdfTextCaret> anchor;
    std::optional<PdfTextCaret> anchor_far;
    PagePoint anchor_origin;
    // Range, Word, Line: the point asked about.
    PagePoint focus;
    // Pages.
    int first_page{0};
    int last_page{0};
    std::uint64_t epoch{0};
  };

private:
  void stop_worker();
  void submit(Request request);
  void pump();
  void drop_selection();

  QPdfDocument* document_{nullptr};
  std::vector<AreasPtr> areas_;
  std::vector<int> wanted_;

  // The ends of the selection: where it started and where it reaches. After a word, a line
  // or whole pages the anchor is the start and anchor_far_ the end.
  std::optional<PdfTextCaret> anchor_;
  std::optional<PdfTextCaret> anchor_far_;
  std::optional<PagePoint> anchor_origin_;
  std::vector<PiecePtr> pieces_;

  std::optional<Request> pending_;
  std::future<void> in_flight_;
  std::shared_ptr<std::atomic<bool>> cancel_;
  bool busy_{false};
  // Counts the gestures (a click, a word, a clear): an answer to an older one is dropped.
  std::uint64_t epoch_{0};
  std::uint64_t document_generation_{0};
};

}  // namespace patchy::ui
