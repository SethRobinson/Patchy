#pragma once

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <future>
#include <vector>

class QPdfDocument;

namespace patchy::ui {

// Links and the outline of a PDF in the viewer (docs/pdf-viewer.md, "Links" and "Outline").

// Where a jump inside the document lands: a page and a point on it, in points from the top
// left of the page as stored (before the view's rotation). The point is (0, 0), the top of
// the page, when the PDF names none.
struct PdfJumpTarget {
  int page{-1};
  QPointF location;
  [[nodiscard]] bool valid() const { return page >= 0; }
  [[nodiscard]] bool operator==(const PdfJumpTarget&) const = default;
};

// One clickable area of a page: `area` in points from the top left of the page as stored,
// and either a place in this document or an address.
struct PdfPageLink {
  QRectF area;
  PdfJumpTarget target;
  QUrl url;
  [[nodiscard]] bool internal() const { return target.valid(); }
  [[nodiscard]] bool operator==(const PdfPageLink&) const = default;
};

// The only addresses the viewer ever hands to the system: http and https with a host, and
// mailto. Everything else (file:, javascript:, a program to launch, a relative address) can
// be read and copied, never opened.
[[nodiscard]] bool pdf_link_url_opens(const QUrl& url);

// What Copy Link Address copies, and what a tooltip shows.
[[nodiscard]] QString pdf_link_address(const QUrl& url);

struct PdfOutlineEntry {
  QString title;
  PdfJumpTarget target;  // not valid for an entry that goes nowhere in this document
  std::vector<PdfOutlineEntry> children;
};

// The document's outline (its bookmarks), top level first. Takes PDFium's lock: call it
// before the page renders start.
[[nodiscard]] std::vector<PdfOutlineEntry> read_pdf_outline(QPdfDocument& document);

// The links of one open PDF, read a page at a time on a worker (PDFium serializes every
// call, and the page renders share that lock), for the pages the view asks for. A page's
// links never change once read.
//
// The document is not owned and must outlive this object, or be detached first with
// set_document(nullptr).
class PdfLinkIndex final : public QObject {
  Q_OBJECT

public:
  explicit PdfLinkIndex(QObject* parent = nullptr);
  ~PdfLinkIndex() override;

  void set_document(QPdfDocument* document);

  // The pages whose links are wanted, most wanted first (the pages on screen).
  void request_pages(const std::vector<int>& pages);
  // A page's links, in the order a click tries them; nullptr until they are read.
  [[nodiscard]] const std::vector<PdfPageLink>* page_links(int page) const;

  // Tests: true while a page is queued or being read.
  [[nodiscard]] bool busy() const;

signals:
  void page_links_ready(int page);

private:
  struct PageState {
    bool read{false};
    std::vector<PdfPageLink> links;
  };

  void stop_worker();
  void pump();

  QPdfDocument* document_{nullptr};
  std::vector<PageState> pages_;
  std::vector<int> wanted_;
  std::future<void> in_flight_;
  bool busy_{false};
  std::uint64_t document_generation_{0};
};

}  // namespace patchy::ui
