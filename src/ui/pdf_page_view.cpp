#include "ui/pdf_page_view.hpp"

#include "ui/background_workers.hpp"
#include "ui/theme_palette.hpp"
#include "ui/ui_profile.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QScrollBar>
#include <QTransform>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <string>

namespace patchy::ui {

namespace {

// A whole-page image holds at most this many device pixels; zoomed further, the visible part
// of the page is rendered sharp on its own (a "detail" image) over the whole-page one.
constexpr double kMaxBasePixels = 8.0e6;
constexpr std::size_t kCacheBudgetBytes = std::size_t{512} * 1024U * 1024U;
// After a wheel page turn, wheel input is ignored this long, so the tail of a trackpad
// fling cannot turn several pages.
constexpr qint64 kFlipCooldownMs = 350;
constexpr double kMinZoomFactor = 0.05;
constexpr double kMaxZoomFactor = 16.0;

int normalized_turns(int turns) {
  return ((turns % 4) + 4) % 4;
}

QSize unturned_size(QSize turned, int turns) {
  return normalized_turns(turns) % 2 == 1 ? turned.transposed() : turned;
}

// The rectangle of the unturned page that becomes `clip` of the page turned clockwise.
QRect unturned_clip(const QRect& clip, QSize turned_full, int turns) {
  const QSize full = unturned_size(turned_full, turns);
  const int w = full.width();
  const int h = full.height();
  const int x0 = clip.x();
  const int x1 = clip.x() + clip.width();
  const int y0 = clip.y();
  const int y1 = clip.y() + clip.height();
  switch (normalized_turns(turns)) {
    case 1:
      return QRect(QPoint(y0, h - x1), QPoint(y1 - 1, h - x0 - 1));
    case 2:
      return QRect(QPoint(w - x1, h - y1), QPoint(w - x0 - 1, h - y0 - 1));
    case 3:
      return QRect(QPoint(w - y1, x0), QPoint(w - y0 - 1, x1 - 1));
    default:
      return clip;
  }
}

// Renders on a worker. PDFium paints no paper, so the result is composited on white and
// handed back opaque, which also makes it cheap to draw.
QImage render_page(QPdfDocument* document, int page, int turns, QSize turned_full, const QRect& clip) {
  const std::string detail = "page=" + std::to_string(page + 1);
  const UiProfileScope profile_scope("pdf_viewer.render", detail);
  const QSize full = unturned_size(turned_full, turns);
  QPdfDocumentRenderOptions options;
  options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
  QImage rendered;
  if (clip.isEmpty()) {
    rendered = document->render(page, full, options);
  } else {
    const QRect source = unturned_clip(clip, turned_full, turns);
    options.setScaledSize(full);
    options.setScaledClipRect(source);
    rendered = document->render(page, source.size(), options);
  }
  if (rendered.isNull()) {
    return {};
  }
  if (normalized_turns(turns) != 0) {
    rendered = rendered.transformed(QTransform().rotate(90.0 * normalized_turns(turns)));
  }
  QImage opaque(rendered.size(), QImage::Format_RGB32);
  opaque.fill(Qt::white);  // color-literal-ok: the paper a page is printed on
  QPainter painter(&opaque);
  painter.drawImage(0, 0, rendered);
  painter.end();
  return opaque;
}

bool same_size_within(QSize a, QSize b, double tolerance) {
  if (b.width() <= 0) {
    return false;
  }
  return std::abs(a.width() - b.width()) <= std::max(1.0, tolerance * b.width());
}

QSize device_size(const PageViewRect& rect, double dpr) {
  return {std::max(1, static_cast<int>(std::lround(rect.width * dpr))),
          std::max(1, static_cast<int>(std::lround(rect.height * dpr)))};
}

QSize capped_base_size(QSize full) {
  const double area = static_cast<double>(full.width()) * full.height();
  if (area <= kMaxBasePixels) {
    return full;
  }
  const double scale = std::sqrt(kMaxBasePixels / area);
  return {std::max(1, static_cast<int>(std::lround(full.width() * scale))),
          std::max(1, static_cast<int>(std::lround(full.height() * scale)))};
}

}  // namespace

PdfPageView::PdfPageView(QWidget* parent) : QAbstractScrollArea(parent) {
  setObjectName(QStringLiteral("pdfViewerPageView"));
  setFrameShape(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
  horizontalScrollBar()->setSingleStep(48);
  verticalScrollBar()->setSingleStep(48);
  schedule_timer_.setSingleShot(true);
  schedule_timer_.setInterval(0);
  connect(&schedule_timer_, &QTimer::timeout, this, &PdfPageView::update_wanted_renders);
  update_scroll_policies();
}

PdfPageView::~PdfPageView() {
  stop_worker();
}

void PdfPageView::stop_worker() {
  if (in_flight_.valid()) {
    in_flight_.wait();
  }
}

void PdfPageView::set_document(QPdfDocument* document) {
  stop_worker();
  ++document_generation_;
  document_ = document;
  base_images_.clear();
  detail_images_.clear();
  wanted_.clear();
  thumbnails_.clear();
  page_sizes_.clear();
  if (document_ != nullptr) {
    const int count = document_->pageCount();
    page_sizes_.reserve(static_cast<std::size_t>(std::max(0, count)));
    for (int page = 0; page < count; ++page) {
      const QSizeF size = document_->pagePointSize(page);
      page_sizes_.push_back({size.width(), size.height()});
    }
  }
  spreads_ = page_spreads(page_count(), options_.spread, options_.direction);
  paged_row_ = 0;
  last_reported_page_ = -1;
  relayout();
  horizontalScrollBar()->setValue(0);
  verticalScrollBar()->setValue(0);
  notify_current_page();
  schedule_renders();
}

double PdfPageView::base_pixels_per_point() const {
  return std::max(1, logicalDpiX()) / 72.0;
}

double PdfPageView::pixels_per_point() const {
  return base_pixels_per_point() * zoom_factor_;
}

double PdfPageView::zoom_percent() const {
  return zoom_factor_ * 100.0;
}

int PdfPageView::row_at_viewport_middle() const {
  if (layout_.rows.empty()) {
    return 0;
  }
  const double middle = verticalScrollBar()->value() + viewport()->height() / 2.0;
  for (std::size_t index = 0; index < layout_.rows.size(); ++index) {
    if (middle < layout_.rows[index].rect.bottom() + options_.row_gap / 2.0) {
      return layout_.first_row + static_cast<int>(index);
    }
  }
  return layout_.first_row + static_cast<int>(layout_.rows.size()) - 1;
}

int PdfPageView::current_row() const {
  if (spreads_.empty()) {
    return 0;
  }
  return std::clamp(continuous_ ? row_at_viewport_middle() : paged_row_, 0, static_cast<int>(spreads_.size()) - 1);
}

int PdfPageView::current_page() const {
  const auto pages = current_pages();
  return pages.empty() ? 0 : pages.front();
}

std::vector<int> PdfPageView::current_pages() const {
  if (spreads_.empty()) {
    return {};
  }
  auto pages = spreads_[static_cast<std::size_t>(current_row())].pages;
  std::sort(pages.begin(), pages.end());
  return pages;
}

QPointF PdfPageView::content_origin() const {
  return {-static_cast<double>(horizontalScrollBar()->value()), -static_cast<double>(verticalScrollBar()->value())};
}

PdfPageView::Anchor PdfPageView::anchor_at(QPointF viewport_point) const {
  Anchor anchor;
  anchor.viewport = viewport_point;
  if (layout_.pages.empty()) {
    return anchor;
  }
  const QPointF content = viewport_point - content_origin();
  double best = 0.0;
  const PageViewSlot* chosen = nullptr;
  for (const auto& slot : layout_.pages) {
    const double dx = std::max({slot.rect.x - content.x(), 0.0, content.x() - slot.rect.right()});
    const double dy = std::max({slot.rect.y - content.y(), 0.0, content.y() - slot.rect.bottom()});
    const double distance = dx * dx + dy * dy;
    if (chosen == nullptr || distance < best) {
      best = distance;
      chosen = &slot;
    }
  }
  anchor.page = chosen->page;
  anchor.fx = (content.x() - chosen->rect.x) / std::max(1.0, chosen->rect.width);
  anchor.fy = (content.y() - chosen->rect.y) / std::max(1.0, chosen->rect.height);
  return anchor;
}

void PdfPageView::restore_anchor(const Anchor& anchor) {
  for (const auto& slot : layout_.pages) {
    if (slot.page != anchor.page) {
      continue;
    }
    const double x = slot.rect.x + anchor.fx * slot.rect.width - anchor.viewport.x();
    const double y = slot.rect.y + anchor.fy * slot.rect.height - anchor.viewport.y();
    horizontalScrollBar()->setValue(static_cast<int>(std::lround(x)));
    verticalScrollBar()->setValue(static_cast<int>(std::lround(y)));
    return;
  }
}

void PdfPageView::update_scroll_policies() {
  // Fitting never overflows across, and a scroll bar that came and went with the fit would
  // change the width it fits to, so the bars that could flicker are fixed on or off.
  const bool fitted = fit_mode_ != PageFitMode::None;
  setHorizontalScrollBarPolicy(fitted ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
  if (!continuous_ && fit_mode_ == PageFitMode::WholePage) {
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  } else if (continuous_ || fit_mode_ == PageFitMode::Width) {
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  } else {
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

void PdfPageView::apply_fit(int reference_row) {
  if (fit_mode_ == PageFitMode::None || spreads_.empty()) {
    return;
  }
  const auto& spread = spreads_[static_cast<std::size_t>(std::clamp(reference_row, 0, static_cast<int>(spreads_.size()) - 1))];
  const double fit = fit_pixels_per_point(page_sizes_, spread, options_, fit_mode_, viewport()->width(),
                                          viewport()->height());
  if (fit > 0.0) {
    zoom_factor_ = std::clamp(fit / base_pixels_per_point(), kMinZoomFactor, kMaxZoomFactor);
  }
}

void PdfPageView::update_scroll_ranges() {
  const int width = viewport()->width();
  const int height = viewport()->height();
  auto* horizontal = horizontalScrollBar();
  auto* vertical = verticalScrollBar();
  // A fit lands on the viewport size give or take rounding: that is no overflow, or the
  // arrow keys would scroll one pixel instead of turning the page.
  const auto overflow = [](double content, int viewport) {
    return std::max(0, static_cast<int>(std::ceil(content - 0.5)) - viewport);
  };
  horizontal->setRange(0, overflow(layout_.width, width));
  vertical->setRange(0, overflow(layout_.height, height));
  horizontal->setPageStep(std::max(1, width));
  vertical->setPageStep(std::max(1, height - 48));
}

void PdfPageView::relayout(const std::optional<Anchor>& anchor) {
  const int reference = current_row();
  const double before = zoom_factor_;
  apply_fit(reference);
  std::optional<int> only_row;
  if (!continuous_) {
    only_row = paged_row_;
  }
  layout_ = layout_page_view(page_sizes_, spreads_, options_, pixels_per_point(), viewport()->width(),
                             viewport()->height(), only_row);
  update_scroll_ranges();
  if (anchor.has_value() && anchor->page >= 0) {
    restore_anchor(*anchor);
  }
  // Detail images belong to one zoom; any other zoom drops them.
  detail_images_.clear();
  viewport()->update();
  if (std::abs(before - zoom_factor_) > 1e-9) {
    emit zoom_changed(zoom_percent());
  }
  schedule_renders();
}

void PdfPageView::scroll_to_row_top(int row) {
  for (std::size_t index = 0; index < layout_.rows.size(); ++index) {
    if (layout_.first_row + static_cast<int>(index) == row) {
      verticalScrollBar()->setValue(static_cast<int>(std::lround(layout_.rows[index].rect.y - options_.margin)));
      return;
    }
  }
}

void PdfPageView::set_current_row(int row, bool at_end) {
  if (spreads_.empty()) {
    return;
  }
  row = std::clamp(row, 0, static_cast<int>(spreads_.size()) - 1);
  if (continuous_) {
    scroll_to_row_top(row);
    notify_current_page();
    return;
  }
  paged_row_ = row;
  relayout();
  verticalScrollBar()->setValue(at_end ? verticalScrollBar()->maximum() : 0);
  auto* horizontal = horizontalScrollBar();
  horizontal->setValue(options_.direction == PageReadingDirection::RightToLeft ? horizontal->maximum() : 0);
  viewport()->update();
  notify_current_page();
}

void PdfPageView::go_to_page(int page) {
  if (spreads_.empty()) {
    return;
  }
  set_current_row(spread_index_for_page(spreads_, std::clamp(page, 0, page_count() - 1)), false);
}

void PdfPageView::next_row() {
  set_current_row(current_row() + 1, false);
}

void PdfPageView::previous_row() {
  set_current_row(current_row() - 1, false);
}

void PdfPageView::first_page() {
  set_current_row(0, false);
}

void PdfPageView::last_page() {
  set_current_row(static_cast<int>(spreads_.size()) - 1, false);
}

void PdfPageView::set_spread_mode(PageSpreadMode mode) {
  if (mode == options_.spread) {
    return;
  }
  const int page = current_page();
  options_.spread = mode;
  spreads_ = page_spreads(page_count(), options_.spread, options_.direction);
  paged_row_ = spread_index_for_page(spreads_, page);
  relayout();
  go_to_page(page);
  emit view_options_changed();
}

void PdfPageView::set_reading_direction(PageReadingDirection direction) {
  if (direction == options_.direction) {
    return;
  }
  const int page = current_page();
  options_.direction = direction;
  spreads_ = page_spreads(page_count(), options_.spread, options_.direction);
  paged_row_ = spread_index_for_page(spreads_, page);
  relayout();
  go_to_page(page);
  emit view_options_changed();
}

void PdfPageView::set_continuous(bool continuous) {
  if (continuous == continuous_) {
    return;
  }
  const int page = current_page();
  continuous_ = continuous;
  paged_row_ = spread_index_for_page(spreads_, page);
  update_scroll_policies();
  relayout();
  go_to_page(page);
  emit view_options_changed();
}

void PdfPageView::rotate_clockwise() {
  const int page = current_page();
  options_.quarter_turns = normalized_turns(options_.quarter_turns + 1);
  relayout();
  go_to_page(page);
  emit view_options_changed();
}

void PdfPageView::set_fit_mode(PageFitMode mode) {
  const int page = current_page();
  fit_mode_ = mode;
  update_scroll_policies();
  relayout();
  go_to_page(page);
  emit zoom_changed(zoom_percent());
}

void PdfPageView::zoom_about(double new_pixels_per_point, QPointF viewport_anchor) {
  const Anchor anchor = anchor_at(viewport_anchor);
  fit_mode_ = PageFitMode::None;
  zoom_factor_ = std::clamp(new_pixels_per_point / base_pixels_per_point(), kMinZoomFactor, kMaxZoomFactor);
  update_scroll_policies();
  relayout(anchor);
  emit zoom_changed(zoom_percent());
  notify_current_page();
}

void PdfPageView::set_zoom_percent(double percent) {
  const QPointF centre(viewport()->width() / 2.0, viewport()->height() / 2.0);
  zoom_about(base_pixels_per_point() * percent / 100.0, centre);
}

void PdfPageView::zoom_in() {
  set_zoom_percent(next_page_view_zoom(zoom_percent(), 1));
}

void PdfPageView::zoom_out() {
  set_zoom_percent(next_page_view_zoom(zoom_percent(), -1));
}

void PdfPageView::notify_current_page() {
  const int page = current_page();
  if (page != last_reported_page_) {
    last_reported_page_ = page;
    emit current_page_changed(page);
  }
}

void PdfPageView::request_thumbnail(int page, QSize size) {
  if (document_ == nullptr || page < 0 || page >= page_count() || size.isEmpty()) {
    return;
  }
  const PageSizePoints turned =
      turned_page_size(page_sizes_[static_cast<std::size_t>(page)], options_.quarter_turns);
  if (!(turned.width > 0.0) || !(turned.height > 0.0)) {
    return;
  }
  const double scale = std::min(size.width() / turned.width, size.height() / turned.height);
  RenderRequest request;
  request.page = page;
  request.turns = options_.quarter_turns;
  request.full_size = QSize(std::max(1, static_cast<int>(std::lround(turned.width * scale))),
                            std::max(1, static_cast<int>(std::lround(turned.height * scale))));
  request.thumbnail = true;
  std::erase_if(thumbnails_, [page](const RenderRequest& queued) { return queued.page == page; });
  // The newest request is the row the sidebar just scrolled to: it goes first.
  thumbnails_.insert(thumbnails_.begin(), request);
  schedule_renders();
}

bool PdfPageView::render_pending() const {
  return busy_ || !wanted_.empty() || !thumbnails_.empty() || schedule_timer_.isActive();
}

int PdfPageView::cached_width_for_testing(int page) const {
  int width = 0;
  if (const auto it = base_images_.find(page); it != base_images_.end()) {
    width = it->second.full_size.width();
  }
  if (const auto it = detail_images_.find(page); it != detail_images_.end()) {
    width = std::max(width, it->second.full_size.width());
  }
  return width;
}

void PdfPageView::schedule_renders() {
  if (!schedule_timer_.isActive()) {
    schedule_timer_.start();
  }
}

bool PdfPageView::covers(const RenderRequest& request) const {
  if (request.clip.isEmpty()) {
    const auto it = base_images_.find(request.page);
    return it != base_images_.end() && it->second.turns == request.turns &&
           same_size_within(it->second.full_size, request.full_size, 0.02);
  }
  const auto it = detail_images_.find(request.page);
  return it != detail_images_.end() && it->second.turns == request.turns &&
         it->second.full_size == request.full_size && it->second.clip.contains(request.clip);
}

void PdfPageView::update_wanted_renders() {
  wanted_.clear();
  protected_pages_.clear();
  if (document_ == nullptr || layout_.pages.empty()) {
    pump_renders();
    return;
  }
  const double dpr = viewport()->devicePixelRatioF();
  const QRectF visible(horizontalScrollBar()->value(), verticalScrollBar()->value(), viewport()->width(),
                       viewport()->height());
  const QPointF centre = visible.center();
  std::vector<const PageViewSlot*> on_screen;
  for (const auto& slot : layout_.pages) {
    const QRectF rect(slot.rect.x, slot.rect.y, slot.rect.width, slot.rect.height);
    if (rect.intersects(visible)) {
      on_screen.push_back(&slot);
    }
  }
  std::sort(on_screen.begin(), on_screen.end(), [&centre](const PageViewSlot* a, const PageViewSlot* b) {
    const auto distance = [&centre](const PageViewSlot* slot) {
      const double dx = slot->rect.x + slot->rect.width / 2.0 - centre.x();
      const double dy = slot->rect.y + slot->rect.height / 2.0 - centre.y();
      return dx * dx + dy * dy;
    };
    return distance(a) < distance(b);
  });
  const auto want = [this](const RenderRequest& request) {
    protected_pages_.push_back(request.page);
    if (!covers(request)) {
      wanted_.push_back(request);
    }
  };
  // 1. Every page on screen, whole, nearest the middle first.
  for (const auto* slot : on_screen) {
    RenderRequest request;
    request.page = slot->page;
    request.turns = options_.quarter_turns;
    request.full_size = capped_base_size(device_size(slot->rect, dpr));
    want(request);
  }
  // 2. The visible part of pages zoomed past a whole-page image, a quarter window around it.
  std::vector<int> detailed;
  for (const auto* slot : on_screen) {
    const QSize full = device_size(slot->rect, dpr);
    if (capped_base_size(full) == full) {
      continue;
    }
    const QRectF page_rect(slot->rect.x, slot->rect.y, slot->rect.width, slot->rect.height);
    const QRectF around = visible.adjusted(-visible.width() / 4.0, -visible.height() / 4.0, visible.width() / 4.0,
                                           visible.height() / 4.0)
                              .intersected(page_rect);
    const QRectF needed = visible.intersected(page_rect);
    const auto to_device = [&](const QRectF& rect) {
      return QRect(QPoint(static_cast<int>(std::floor((rect.left() - page_rect.left()) * dpr)),
                          static_cast<int>(std::floor((rect.top() - page_rect.top()) * dpr))),
                   QPoint(static_cast<int>(std::ceil((rect.right() - page_rect.left()) * dpr)) - 1,
                          static_cast<int>(std::ceil((rect.bottom() - page_rect.top()) * dpr)) - 1))
          .intersected(QRect(QPoint(0, 0), full));
    };
    RenderRequest request;
    request.page = slot->page;
    request.turns = options_.quarter_turns;
    request.full_size = full;
    request.clip = to_device(needed);
    detailed.push_back(slot->page);
    if (request.clip.isEmpty() || covers(request)) {
      continue;
    }
    request.clip = to_device(around);
    wanted_.push_back(request);
  }
  std::erase_if(detail_images_, [&detailed](const auto& entry) {
    return std::find(detailed.begin(), detailed.end(), entry.first) == detailed.end();
  });
  // 3. Prefetch: the rows either side, so a page turn shows a sharp page at once.
  if (continuous_) {
    const QRectF nearby = visible.adjusted(0.0, -visible.height(), 0.0, visible.height());
    for (const auto& slot : layout_.pages) {
      const QRectF rect(slot.rect.x, slot.rect.y, slot.rect.width, slot.rect.height);
      if (!rect.intersects(visible) && rect.intersects(nearby)) {
        RenderRequest request;
        request.page = slot.page;
        request.turns = options_.quarter_turns;
        request.full_size = capped_base_size(device_size(slot.rect, dpr));
        want(request);
      }
    }
  } else {
    for (const int row : {paged_row_ + 1, paged_row_ - 1, paged_row_ + 2}) {
      if (row < 0 || row >= static_cast<int>(spreads_.size())) {
        continue;
      }
      double scale = pixels_per_point();
      if (fit_mode_ != PageFitMode::None) {
        const double fit = fit_pixels_per_point(page_sizes_, spreads_[static_cast<std::size_t>(row)], options_,
                                                fit_mode_, viewport()->width(), viewport()->height());
        if (fit > 0.0) {
          scale = fit;
        }
      }
      for (const int page : spreads_[static_cast<std::size_t>(row)].pages) {
        const PageSizePoints size = turned_page_size(page_sizes_[static_cast<std::size_t>(page)], options_.quarter_turns);
        RenderRequest request;
        request.page = page;
        request.turns = options_.quarter_turns;
        request.full_size = capped_base_size(device_size({0.0, 0.0, size.width * scale, size.height * scale}, dpr));
        want(request);
      }
    }
  }
  pump_renders();
}

void PdfPageView::pump_renders() {
  if (busy_ || document_ == nullptr) {
    return;
  }
  RenderRequest request;
  if (!wanted_.empty()) {
    request = wanted_.front();
    wanted_.erase(wanted_.begin());
  } else if (!thumbnails_.empty()) {
    request = thumbnails_.front();
    thumbnails_.erase(thumbnails_.begin());
  } else {
    return;
  }
  busy_ = true;
  QPdfDocument* document = document_;
  const std::uint64_t generation = document_generation_;
  // PDFium serializes every call behind Qt PDF's lock, so one worker is all a document can
  // use. The destructor and set_document wait for it before the document can go away.
  in_flight_ = launch_async([this, document, request, generation] {
    QImage image;
    try {
      image = render_page(document, request.page, request.turns, request.full_size, request.clip);
    } catch (...) {
      image = QImage();
    }
    QMetaObject::invokeMethod(
        this,
        [this, request, image = std::move(image), generation] {
          busy_ = false;
          if (generation == document_generation_) {
            on_rendered(request, image);
          }
          update_wanted_renders();
        },
        Qt::QueuedConnection);
  });
}

void PdfPageView::on_rendered(const RenderRequest& request, const QImage& image) {
  if (request.thumbnail) {
    if (!image.isNull()) {
      emit thumbnail_ready(request.page, image);
    }
    return;
  }
  CachedImage cached;
  cached.image = image;  // a failed render stays null: drawn as a blank page, never retried
  cached.turns = request.turns;
  cached.full_size = request.full_size;
  cached.clip = request.clip;
  cached.last_used = ++use_clock_;
  if (request.clip.isEmpty()) {
    base_images_[request.page] = std::move(cached);
  } else {
    detail_images_[request.page] = std::move(cached);
  }
  evict_cache();
  viewport()->update();
}

void PdfPageView::evict_cache() {
  std::size_t total = 0;
  for (const auto& [page, cached] : base_images_) {
    total += static_cast<std::size_t>(cached.image.sizeInBytes());
  }
  for (const auto& [page, cached] : detail_images_) {
    total += static_cast<std::size_t>(cached.image.sizeInBytes());
  }
  while (total > kCacheBudgetBytes) {
    auto oldest = base_images_.end();
    for (auto it = base_images_.begin(); it != base_images_.end(); ++it) {
      if (std::find(protected_pages_.begin(), protected_pages_.end(), it->first) != protected_pages_.end()) {
        continue;
      }
      if (oldest == base_images_.end() || it->second.last_used < oldest->second.last_used) {
        oldest = it;
      }
    }
    if (oldest == base_images_.end()) {
      return;
    }
    total -= static_cast<std::size_t>(oldest->second.image.sizeInBytes());
    base_images_.erase(oldest);
  }
}

void PdfPageView::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(viewport());
  const auto& palette = theme();
  painter.fillRect(viewport()->rect(), palette.canvas_backdrop);
  if (layout_.pages.empty()) {
    return;
  }
  const QPointF origin = content_origin();
  const QRectF visible(QPointF(0.0, 0.0), QSizeF(viewport()->size()));
  std::vector<std::pair<const PageViewSlot*, QRectF>> drawn;
  for (const auto& slot : layout_.pages) {
    const QRectF target = QRectF(slot.rect.x, slot.rect.y, slot.rect.width, slot.rect.height).translated(origin);
    if (target.intersects(visible)) {
      drawn.emplace_back(&slot, target);
    }
  }
  // Outlines first and pages over them, so the two pages of a spread meet without a line.
  painter.setPen(Qt::NoPen);
  painter.setBrush(palette.canvas_document_border);
  for (const auto& [slot, target] : drawn) {
    painter.drawRect(target.adjusted(-1.0, -1.0, 1.0, 1.0));
  }
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  for (const auto& [slot, target] : drawn) {
    painter.fillRect(target, Qt::white);  // color-literal-ok: the paper a page is printed on
    if (auto it = base_images_.find(slot->page); it != base_images_.end() && !it->second.image.isNull()) {
      it->second.last_used = ++use_clock_;
      const QImage& image = it->second.image;
      const int delta = normalized_turns(options_.quarter_turns - it->second.turns);
      if (delta == 0) {
        painter.drawImage(target, image);
      } else {
        // Rotated a moment ago: show the old render turned until the new one arrives.
        painter.save();
        painter.translate(target.center());
        painter.rotate(90.0 * delta);
        const QSizeF size = delta % 2 == 1 ? target.size().transposed() : target.size();
        painter.drawImage(QRectF(QPointF(-size.width() / 2.0, -size.height() / 2.0), size), image);
        painter.restore();
      }
    }
    if (auto it = detail_images_.find(slot->page); it != detail_images_.end() && !it->second.image.isNull() &&
                                                   it->second.turns == options_.quarter_turns) {
      const CachedImage& detail = it->second;
      const double scale = target.width() / std::max(1, detail.full_size.width());
      const QRectF dest(target.x() + detail.clip.x() * scale, target.y() + detail.clip.y() * scale,
                        detail.clip.width() * scale, detail.clip.height() * scale);
      painter.drawImage(dest, detail.image);
    }
  }
}

void PdfPageView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  // The point of the page at the top left stays there, whatever the new zoom.
  const Anchor anchor = anchor_at(QPointF(0.0, 0.0));
  relayout(continuous_ ? std::optional<Anchor>(anchor) : std::nullopt);
  notify_current_page();
}

void PdfPageView::wheelEvent(QWheelEvent* event) {
  if (event->modifiers().testFlag(Qt::ControlModifier)) {
    const double steps = event->angleDelta().y() / 120.0;
    if (steps != 0.0) {
      zoom_about(pixels_per_point() * std::pow(1.2, steps), event->position());
    }
    event->accept();
    return;
  }
  const int dy = event->angleDelta().y();
  if (continuous_ || dy == 0 || event->modifiers().testFlag(Qt::ShiftModifier)) {
    QAbstractScrollArea::wheelEvent(event);
    return;
  }
  // One row at a time: scroll inside a zoomed page first, then turn the page at its edge.
  auto* vertical = verticalScrollBar();
  const bool at_edge = dy < 0 ? vertical->value() >= vertical->maximum() : vertical->value() <= vertical->minimum();
  if (!at_edge) {
    wheel_flip_accumulator_ = 0.0;
    QAbstractScrollArea::wheelEvent(event);
    return;
  }
  event->accept();
  if (last_flip_.isValid() && last_flip_.elapsed() < kFlipCooldownMs) {
    last_flip_.restart();
    return;
  }
  wheel_flip_accumulator_ += dy;
  if (std::abs(wheel_flip_accumulator_) < 120.0) {
    return;
  }
  const bool forward = wheel_flip_accumulator_ < 0.0;
  wheel_flip_accumulator_ = 0.0;
  const int row = current_row() + (forward ? 1 : -1);
  if (row < 0 || row >= static_cast<int>(spreads_.size())) {
    return;
  }
  set_current_row(row, !forward);
  last_flip_.restart();
}

void PdfPageView::keyPressEvent(QKeyEvent* event) {
  auto* vertical = verticalScrollBar();
  auto* horizontal = horizontalScrollBar();
  const bool rtl = options_.direction == PageReadingDirection::RightToLeft;
  const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
  const auto page_forward = [&] {
    if (!continuous_ && vertical->value() < vertical->maximum()) {
      vertical->setValue(vertical->value() + vertical->pageStep());
    } else if (continuous_) {
      vertical->setValue(vertical->value() + vertical->pageStep());
    } else {
      next_row();
    }
  };
  const auto page_back = [&] {
    if (!continuous_ && vertical->value() > vertical->minimum()) {
      vertical->setValue(vertical->value() - vertical->pageStep());
    } else if (continuous_) {
      vertical->setValue(vertical->value() - vertical->pageStep());
    } else if (current_row() > 0) {
      set_current_row(current_row() - 1, true);
    }
  };
  switch (event->key()) {
    case Qt::Key_PageDown:
      page_forward();
      break;
    case Qt::Key_PageUp:
      page_back();
      break;
    case Qt::Key_Space:
      shift ? page_back() : page_forward();
      break;
    case Qt::Key_Home:
      first_page();
      break;
    case Qt::Key_End:
      last_page();
      break;
    case Qt::Key_Right:
    case Qt::Key_Left: {
      if (horizontal->maximum() > 0) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
      }
      const bool towards_right = event->key() == Qt::Key_Right;
      (towards_right != rtl) ? next_row() : previous_row();
      break;
    }
    case Qt::Key_Down:
    case Qt::Key_Up:
      if (!continuous_ && vertical->maximum() == 0) {
        event->key() == Qt::Key_Down ? next_row() : previous_row();
        break;
      }
      QAbstractScrollArea::keyPressEvent(event);
      return;
    case Qt::Key_Escape:
      emit escape_pressed();
      break;
    default:
      QAbstractScrollArea::keyPressEvent(event);
      return;
  }
  event->accept();
}

void PdfPageView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
    panning_ = true;
    pan_last_ = event->position().toPoint();
    viewport()->setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QAbstractScrollArea::mousePressEvent(event);
}

void PdfPageView::mouseMoveEvent(QMouseEvent* event) {
  if (panning_) {
    const QPoint position = event->position().toPoint();
    const QPoint delta = position - pan_last_;
    pan_last_ = position;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    event->accept();
    return;
  }
  QAbstractScrollArea::mouseMoveEvent(event);
}

void PdfPageView::mouseReleaseEvent(QMouseEvent* event) {
  if (panning_) {
    panning_ = false;
    viewport()->unsetCursor();
    event->accept();
    return;
  }
  QAbstractScrollArea::mouseReleaseEvent(event);
}

void PdfPageView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    panning_ = false;
    viewport()->unsetCursor();
    emit full_screen_toggle_requested();
    event->accept();
    return;
  }
  QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void PdfPageView::scrollContentsBy(int dx, int dy) {
  Q_UNUSED(dx);
  Q_UNUSED(dy);
  viewport()->update();
  notify_current_page();
  schedule_renders();
}

}  // namespace patchy::ui
