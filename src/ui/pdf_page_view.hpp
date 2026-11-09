#pragma once

#include "core/page_view_layout.hpp"

#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QTimer>

#include <cstdint>
#include <future>
#include <optional>
#include <unordered_map>
#include <vector>

class QPdfDocument;

namespace patchy::ui {

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

protected:
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

  QPdfDocument* document_{nullptr};
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

  bool panning_{false};
  QPoint pan_last_;
  double wheel_flip_accumulator_{0.0};
  QElapsedTimer last_flip_;
};

}  // namespace patchy::ui
