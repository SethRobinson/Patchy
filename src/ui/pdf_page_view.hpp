#pragma once

#include "core/page_view_layout.hpp"
#include "ui/pdf_links.hpp"

#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QTimer>
#include <QUrl>

#include <cstdint>
#include <future>
#include <optional>
#include <unordered_map>
#include <vector>

class QContextMenuEvent;
class QPainter;
class QPdfDocument;

namespace patchy::ui {

class PdfTextSearch;
class PdfTextSelection;

// Where the view is, for Back and Forward (docs/pdf-viewer.md, "Back and Forward"): the page
// being read and the point of a page that sits at the window's top left corner, as a share
// of that page, so it means the same place at another zoom, layout or rotation.
struct PdfViewPosition {
  int page{0};
  int anchor_page{-1};
  double fx{0.0};
  double fy{0.0};
  [[nodiscard]] bool operator==(const PdfViewPosition&) const = default;
};

// The PDF viewer's page area (docs/pdf-viewer.md): pages laid out by core/page_view_layout,
// rendered by PDFium one page at a time on a worker thread, cached, and drawn scaled while a
// sharper render is on its way, so scrolling and zooming never wait for PDFium.
//
// Built only with Qt PDF (PATCHY_HAVE_QT_PDF). The document is not owned and must outlive the
// view, or be detached first with set_document(nullptr).
class PdfPageView final : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit PdfPageView(QWidget* parent = nullptr);
  ~PdfPageView() override;

  void set_document(QPdfDocument* document);
  [[nodiscard]] int page_count() const { return static_cast<int>(page_sizes_.size()); }

  // The first page in reading order of the row at the middle of the window.
  [[nodiscard]] int current_page() const;
  // The pages of the current row in reading order (one or two).
  [[nodiscard]] std::vector<int> current_pages() const;
  void go_to_page(int page);
  // Shows a place a link or an outline entry names: its page, with the point at the top of
  // the window (and brought into view across, when the page is wider than the window).
  void go_to_target(const PdfJumpTarget& target);
  // Back and Forward: where the view is now, and going back there.
  [[nodiscard]] PdfViewPosition view_position() const;
  void restore_view_position(const PdfViewPosition& position);
  void next_row();
  void previous_row();
  void first_page();
  void last_page();

  void set_spread_mode(PageSpreadMode mode);
  [[nodiscard]] PageSpreadMode spread_mode() const { return options_.spread; }
  void set_reading_direction(PageReadingDirection direction);
  [[nodiscard]] PageReadingDirection reading_direction() const { return options_.direction; }
  // Continuous scrolls through every page; off shows one row (page or spread) at a time and
  // the wheel turns the page at the top and bottom.
  void set_continuous(bool continuous);
  [[nodiscard]] bool continuous() const { return continuous_; }
  void rotate_clockwise();
  [[nodiscard]] int quarter_turns() const { return options_.quarter_turns; }

  void set_fit_mode(PageFitMode mode);
  [[nodiscard]] PageFitMode fit_mode() const { return fit_mode_; }
  // 100 is actual size: a point is 1/72 inch at the screen's logical resolution.
  void set_zoom_percent(double percent);
  [[nodiscard]] double zoom_percent() const;
  void zoom_in();
  void zoom_out();

  // A small page image for the sidebar, rendered after everything on screen. Answered
  // through thumbnail_ready; a request for a page already queued just moves it up.
  void request_thumbnail(int page, QSize size);

  // Find (docs/pdf-viewer.md): the search whose matches are drawn over the pages and whose
  // current match is kept in view. Not owned.
  void set_text_search(PdfTextSearch* search);
  // Brings the search's current match into view: its page at once (turning to it when one
  // row shows at a time), the match itself once its place on the page is known. A match
  // already in view moves nothing; any other goes to the middle of the window.
  void show_current_text_match();
  // Tests: the current match in viewport coordinates; empty until it is placed.
  [[nodiscard]] QRectF current_text_match_rect_for_testing() const;

  // Links (docs/pdf-viewer.md, "Links"). The link under a point of the page area, or
  // nullptr (also while its page's links are still being read).
  [[nodiscard]] const PdfPageLink* link_at(QPointF viewport_point) const;
  // What the tooltip over `link` says: "Page 12", or the address and how to open it.
  [[nodiscard]] QString link_tip(const PdfPageLink& link) const;
  [[nodiscard]] PdfLinkIndex* links() const { return links_; }
  // Tests: where the links of `page` are, in viewport coordinates, in page_links order.
  [[nodiscard]] std::vector<QRectF> link_rects_for_testing(int page) const;

  // Selecting text (docs/pdf-viewer-selection.md). The selection belongs to the
  // view; what it holds is asked through these.
  [[nodiscard]] PdfTextSelection* text_selection() const { return selection_; }
  [[nodiscard]] bool has_text_selection() const;
  [[nodiscard]] QString selected_text() const;
  // Hands the selected text to text_copy_requested; nothing without a selection.
  void copy_selection();
  // All the text of one page, or of the page or spread being read.
  void select_all_on_page(int page);
  void select_all_on_current_pages();
  void clear_text_selection();
  // Tests: where the selection is on `page`, in viewport coordinates.
  [[nodiscard]] std::vector<QRectF> selection_rects_for_testing(int page) const;
  // Tests: a point of the page area as a point of `page`, in points from its top left, and
  // an area of `page` in viewport coordinates.
  [[nodiscard]] QPointF page_point_for_testing(int page, QPointF viewport_point) const;
  [[nodiscard]] QRectF page_area_rect_for_testing(int page, const QRectF& area) const;

  // Tests: true while a render is queued or running.
  [[nodiscard]] bool render_pending() const;
  // Tests: the device-pixel width of the sharpest image held for `page` (0 = none).
  [[nodiscard]] int cached_width_for_testing(int page) const;

signals:
  void current_page_changed(int page);
  void zoom_changed(double percent);
  void view_options_changed();
  void thumbnail_ready(int page, const QImage& image);
  void full_screen_toggle_requested();
  void escape_pressed();
  // A link into the document was clicked (or chosen from its menu): the window jumps there
  // and remembers where it came from.
  void jump_requested(const patchy::ui::PdfJumpTarget& target);
  // An address was Ctrl+clicked or Open Link chosen; only ever one pdf_link_url_opens takes.
  void url_open_requested(const QUrl& url);
  void url_copy_requested(const QUrl& url);
  // Copy was chosen with text selected: the text as the clipboard should get it.
  void text_copy_requested(const QString& text);
  void text_selection_changed();
  // "Recognize Text on This Page..." was chosen on a page without text of its own.
  void recognize_page_requested(int page);

protected:
  bool viewportEvent(QEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;

private:
  // A point of a page that a relayout keeps under the same point of the window.
  struct Anchor {
    int page{-1};
    double fx{0.0};
    double fy{0.0};
    QPointF viewport;
  };
  struct RenderRequest {
    int page{0};
    int turns{0};
    QSize full_size;  // device pixels of the whole (turned) page
    QRect clip;       // part of the turned page to render; empty = all of it
    bool thumbnail{false};
  };
  struct CachedImage {
    QImage image;
    int turns{0};
    QSize full_size;
    QRect clip;  // empty for a whole-page image
    std::uint64_t last_used{0};
  };

  [[nodiscard]] double base_pixels_per_point() const;
  [[nodiscard]] double pixels_per_point() const;
  [[nodiscard]] int current_row() const;
  [[nodiscard]] int row_at_viewport_middle() const;
  [[nodiscard]] QPointF content_origin() const;
  [[nodiscard]] Anchor anchor_at(QPointF viewport_point) const;
  void restore_anchor(const Anchor& anchor);
  void relayout(const std::optional<Anchor>& anchor = std::nullopt);
  void apply_fit(int reference_row);
  void update_scroll_policies();
  void update_scroll_ranges();
  void scroll_to_row_top(int row);
  void set_current_row(int row, bool at_end);
  void zoom_about(double new_pixels_per_point, QPointF viewport_anchor);
  void notify_current_page();
  void schedule_renders();
  void update_wanted_renders();
  void pump_renders();
  void on_rendered(const RenderRequest& request, const QImage& image);
  [[nodiscard]] bool covers(const RenderRequest& request) const;
  void evict_cache();
  void stop_worker();
  [[nodiscard]] const PageViewSlot* slot_for_page(int page) const;
  // An area of a page, in points from its top left as stored, in content coordinates.
  [[nodiscard]] QRectF page_area_to_content(const PageViewSlot& slot, const QRectF& area) const;
  [[nodiscard]] QRectF current_text_match_area() const;
  void paint_text_matches(QPainter& painter, const PageViewSlot& slot, QPointF origin, const QRectF& target);
  void paint_text_selection(QPainter& painter, const PageViewSlot& slot, QPointF origin, const QRectF& target);
  // A point of the page area as a point of a page, in points from the page's top left as
  // stored: the page under it, or (`nearest`) the page nearest to it. page -1: none.
  struct PagePoint {
    int page{-1};
    QPointF point;
  };
  [[nodiscard]] PagePoint page_point_at(QPointF viewport_point, bool nearest) const;
  [[nodiscard]] QPointF content_to_page_point(const PageViewSlot& slot, QPointF content) const;
  [[nodiscard]] bool text_under(QPointF viewport_point) const;
  void begin_text_selection_drag();
  void drag_text_selection_to(QPoint position);
  void scroll_for_selection_drag();
  // The link under a point of the page area and where it is, both in viewport coordinates.
  struct LinkHit {
    const PdfPageLink* link{nullptr};
    QRectF rect;
  };
  [[nodiscard]] LinkHit hit_link(QPointF viewport_point) const;
  void update_hover();
  void activate_link(const PdfPageLink& link, Qt::KeyboardModifiers modifiers, QPoint global_position);
  void show_link_note(QPoint global_position, const QString& text, const QRectF& link_rect);

  QPdfDocument* document_{nullptr};
  QPointer<PdfTextSearch> search_;
  PdfLinkIndex* links_{nullptr};
  PdfTextSelection* selection_{nullptr};
  std::vector<PageSizePoints> page_sizes_;
  std::vector<PageSpread> spreads_;
  PageViewLayoutOptions options_;
  PageViewLayout layout_;
  bool continuous_{true};
  PageFitMode fit_mode_{PageFitMode::WholePage};
  double zoom_factor_{1.0};  // pixels per point / base_pixels_per_point
  int paged_row_{0};
  int last_reported_page_{-1};

  std::unordered_map<int, CachedImage> base_images_;
  // Sharp renders of the visible part of a page zoomed past what a whole-page image may
  // hold (kMaxBasePixels); at most one per page.
  std::unordered_map<int, CachedImage> detail_images_;
  std::vector<int> protected_pages_;  // on screen or prefetched: never evicted
  std::uint64_t use_clock_{0};

  std::vector<RenderRequest> wanted_;
  std::vector<RenderRequest> thumbnails_;
  std::future<void> in_flight_;
  bool busy_{false};
  QTimer schedule_timer_;
  std::uint64_t document_generation_{0};

  // What a press on the pages has become. A press on text selects once the pointer has
  // moved a little; anywhere else it pans as soon as the pointer moves. On a link it stays a
  // click until the pointer has gone a drag's distance, then selects (on text) or pans.
  enum class MouseDrag { None, Pan, Select };
  MouseDrag drag_{MouseDrag::None};
  QPoint press_position_;
  QPoint pan_last_;
  bool press_moved_{false};
  std::optional<PdfPageLink> press_link_;
  // The press was on text, and where on its page.
  bool press_on_text_{false};
  PagePoint press_page_point_;
  // A Select drag that has started selecting (the pointer moved, or Shift was held).
  bool select_started_{false};
  // Where the pointer is during a Select drag, and the timer that scrolls while it is
  // outside the page area.
  QPoint select_position_;
  QTimer select_scroll_timer_;
  // Since the last double-click that selected a word, and where: a third click right behind
  // it selects the line.
  QElapsedTimer word_click_timer_;
  QPoint word_click_position_;
  // Where the pointer was last seen over the pages, for the cursor after a scroll.
  std::optional<QPoint> hover_position_;
  enum class Hover { None, Link, Text };
  Hover hover_{Hover::None};
  // Since the last click that followed a link: a second click right behind it is not a
  // double-click for full screen.
  QElapsedTimer link_click_timer_;
  double wheel_flip_accumulator_{0.0};
  QElapsedTimer last_flip_;
};

}  // namespace patchy::ui
