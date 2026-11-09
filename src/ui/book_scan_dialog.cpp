#include "ui/book_scan_dialog.hpp"

#include "core/photo_divide.hpp"
#include "ui/app_settings.hpp"
#include "ui/book_pdf_export.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/qt_paths.hpp"
#include "ui/theme_palette.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace patchy::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kCoverDpi = 300;
constexpr double kBookAreaMarginInches = 15.0 / 25.4;
constexpr double kBedFitMarginInches = 0.1;
constexpr int kThumbnailDpi = 24;
constexpr int kReviewPreviewDpi = 60;
constexpr double kSmallScanDpi = 36.0;
// The view copy's longest side: the box editor shows at most this many pixels, and the
// review preview (kReviewPreviewDpi) stays under the copy's resolution for any bed.
constexpr int kViewLongestSide = 1600;
constexpr std::size_t kViewCacheCapacity = 12;
constexpr int kViewJpegQuality = 90;
constexpr qint64 kLowDiskBytes = 2LL * 1024 * 1024 * 1024;
constexpr double kEdgeWarningPixels = 3.0;

bool metric_units() {
  return QLocale().measurementSystem() == QLocale::MetricSystem;
}

QString size_text(const BookPageSize& size) {
  if (metric_units()) {
    return QObject::tr("%1 x %2 mm")
        .arg(QLocale().toString(size.width * 25.4, 'f', 0), QLocale().toString(size.height * 25.4, 'f', 0));
  }
  return QObject::tr("%1 x %2 in")
      .arg(QLocale().toString(size.width, 'f', 2), QLocale().toString(size.height, 'f', 2));
}

QString scans_dir_name() {
  return QStringLiteral("scans");
}

QString thumbnail_key(const BookPageRef& page, const BookPageSize& size) {
  return QStringLiteral("%1/%2/%3/%4/%5x%6")
      .arg(static_cast<int>(page.kind))
      .arg(page.scan)
      .arg(page.side == BookPageSide::Right ? 1 : 0)
      .arg(page.turns)
      .arg(std::lround(size.width * 100.0))
      .arg(std::lround(size.height * 100.0));
}

QVariant page_variant(const BookPageRef& page) {
  return QVariantList{static_cast<int>(page.kind), page.scan, page.side == BookPageSide::Right ? 1 : 0, page.turns,
                      page.output ? static_cast<int>(*page.output) : -1};
}

BookPageRef page_from_variant(const QVariant& value) {
  const QVariantList list = value.toList();
  BookPageRef page;
  if (list.size() == 5) {
    page.kind = static_cast<BookPageRef::Kind>(list[0].toInt());
    page.scan = list[1].toInt();
    page.side = list[2].toInt() == 1 ? BookPageSide::Right : BookPageSide::Left;
    page.turns = list[3].toInt();
    if (list[4].toInt() >= 0) {
      page.output = static_cast<BookOutputMode>(list[4].toInt());
    }
  }
  return page;
}

// The view copy of a scan, beside its small copy in thumbs/.
QString view_copy_file(const QString& file) {
  return QStringLiteral("thumbs/") + QFileInfo(file).completeBaseName() + QStringLiteral("-view.jpg");
}

QImage read_scan_image(const QString& path) {
  QImageReader reader(path);
  reader.setAutoTransform(false);
  return reader.read();
}

PixelBuffer upright_pixels(const QImage& image, int turns) {
  const PixelBuffer pixels = book_pixels_from_image(image);
  const int normalized = ((turns % 4) + 4) % 4;
  return normalized == 0 || pixels.empty() ? pixels : rotated_quarter_turns(pixels, normalized);
}

// Removes the files a book project wrote, one by one, then its scans and thumbs folders
// if they are empty and, with `remove_folder`, the project folder too: never a
// recursive delete. Other files in the folder (an exported PDF) stay.
void delete_book_project_files(const BookProject& project, bool remove_folder) {
  const QDir folder(project.folder);
  if (project.folder.isEmpty() || !folder.exists(QString::fromLatin1(kBookProjectFileName))) {
    return;
  }
  const auto remove_scan = [&folder](const QString& file) {
    QFile::remove(folder.filePath(file));
    QFile::remove(folder.filePath(QStringLiteral("thumbs/") + QFileInfo(file).fileName()));
    QFile::remove(folder.filePath(view_copy_file(file)));
  };
  if (project.cover) {
    remove_scan(project.cover->file);
  }
  for (const auto& scan : project.scans) {
    remove_scan(scan.file);
  }
  folder.rmdir(scans_dir_name());
  folder.rmdir(QStringLiteral("thumbs"));
  QFile::remove(folder.filePath(QString::fromLatin1(kBookProjectFileName)));
  if (remove_folder) {
    QDir parent = folder;
    if (parent.cdUp()) {
      parent.rmdir(folder.dirName());
    }
  }
}

// The page sizes the review step offers besides the measured ones (inches).
struct SizePreset {
  const char* token;
  double width;
  double height;
};
constexpr SizePreset kSizePresets[] = {
    {"a4", 210.0 / 25.4, 297.0 / 25.4}, {"a5", 148.0 / 25.4, 210.0 / 25.4}, {"letter", 8.5, 11.0},
    {"6x9", 6.0, 9.0},                  {"5.5x8.5", 5.5, 8.5},
};

}  // namespace

// The scan with its editable boxes: the cover outline and two spine lines, a spread
// and its gutter, or one page. Drag inside a box to move it, near an edge to move that
// edge, near a line to move the line. Coordinates are the full upright scan's.
class BookScanPreview final : public QWidget {
public:
  enum class Kind { Cover, Spread, Page };

  explicit BookScanPreview(QWidget* parent) : QWidget(parent) {
    setMinimumSize(360, 260);
    setMouseTracking(true);
  }

  std::function<void()> on_edited;

  // `display` is a copy of the scan at any scale; boxes are in `full_size` coordinates.
  void set_image(const QImage& display, QSize full_size) {
    full_size_ = full_size;
    const int longest = std::max(display.width(), display.height());
    const double scale = longest > kViewLongestSide ? static_cast<double>(kViewLongestSide) / longest : 1.0;
    display_ = scale < 1.0 ? display.scaled(display.size() * scale, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                           : display;
    update();
  }
  void clear_image() {
    display_ = QImage();
    full_size_ = QSize();
    update();
  }
  void set_kind(Kind kind) { kind_ = kind; }
  void set_box(const OrientedBox& box) {
    box_ = box;
    update();
  }
  void set_lines(double first, double second) {
    line_a_ = first;
    line_b_ = second;
    update();
  }
  // Names drawn over the two outer parts of a cover (the covers either side of the spine).
  void set_region_labels(const QString& left, const QString& right) {
    left_label_ = left;
    right_label_ = right;
    update();
  }
  [[nodiscard]] const OrientedBox& box() const { return box_; }
  [[nodiscard]] double line_a() const { return line_a_; }
  [[nodiscard]] double line_b() const { return line_b_; }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    if (display_.isNull() || full_size_.isEmpty()) {
      return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF target = image_rect();
    painter.drawImage(target, display_);
    const auto corners = oriented_box_corners(box_);
    QPolygonF polygon;
    for (int i = 0; i < 4; ++i) {
      polygon << to_widget({corners[static_cast<std::size_t>(i * 2)], corners[static_cast<std::size_t>(i * 2 + 1)]});
    }
    // Shade what lies outside the box so the kept area stands out.
    QPainterPath outside;
    outside.addRect(target);
    QPainterPath inside;
    inside.addPolygon(polygon);
    painter.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 110));  // color-literal-ok: shade over scan content
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255), 2.0));  // color-literal-ok: outline over scan content
    painter.drawPolygon(polygon);
    painter.setPen(QPen(QColor(0, 0, 0), 1.0, Qt::DashLine));  // color-literal-ok: outline over scan content
    painter.drawPolygon(polygon);
    const auto draw_line = [&](double fraction, const QColor& color) {
      const auto ends = line_ends(fraction);
      painter.setPen(QPen(color, 2.0));
      painter.drawLine(to_widget(ends.first), to_widget(ends.second));
    };
    if (kind_ == Kind::Cover) {
      draw_line(line_a_, QColor(255, 200, 0));  // color-literal-ok: spine marker over scan content
      draw_line(line_b_, QColor(255, 200, 0));  // color-literal-ok: spine marker over scan content
      const double first = std::min(line_a_, line_b_);
      const double second = std::max(line_a_, line_b_);
      draw_label(painter, first / 2.0, left_label_);
      draw_label(painter, (second + 1.0) / 2.0, right_label_);
    } else if (kind_ == Kind::Spread) {
      draw_line(line_a_, QColor(0, 200, 255));  // color-literal-ok: gutter marker over scan content
    }
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton || display_.isNull()) {
      return;
    }
    drag_ = hit_test(event->position());
    press_ = to_image(event->position());
    press_box_ = box_;
    press_line_a_ = line_a_;
    press_line_b_ = line_b_;
  }
  void mouseMoveEvent(QMouseEvent* event) override {
    if (drag_ == Drag::None) {
      const Drag hover = display_.isNull() ? Drag::None : hit_test(event->position());
      setCursor(hover == Drag::Move               ? Qt::SizeAllCursor
                : hover == Drag::None             ? Qt::ArrowCursor
                : (hover == Drag::Top || hover == Drag::Bottom) ? Qt::SizeVerCursor
                                                               : Qt::SizeHorCursor);
      return;
    }
    const QPointF now = to_image(event->position());
    if (drag_ == Drag::LineEither) {
      // Both spine lines under the press: the drag direction picks one.
      const double moved = (now.x() - press_.x()) * std::cos(press_box_.angle_degrees * kPi / 180.0) +
                           (now.y() - press_.y()) * std::sin(press_box_.angle_degrees * kPi / 180.0);
      if (std::abs(moved) < 1e-9) {
        return;
      }
      drag_ = (moved < 0.0) == (line_a_ <= line_b_) ? Drag::LineA : Drag::LineB;
    }
    const double radians = press_box_.angle_degrees * kPi / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const double dx = now.x() - press_.x();
    const double dy = now.y() - press_.y();
    const double along = dx * c + dy * s;    // along the box's x axis
    const double across = -dx * s + dy * c;  // along its y axis
    OrientedBox box = press_box_;
    const auto shift = [&](double local_x, double local_y) {
      box.center_x += local_x * c - local_y * s;
      box.center_y += local_x * s + local_y * c;
    };
    switch (drag_) {
      case Drag::Move:
        box.center_x += dx;
        box.center_y += dy;
        break;
      case Drag::Left:
        box.width = std::max(8.0, press_box_.width - along);
        shift((press_box_.width - box.width) / 2.0, 0.0);
        break;
      case Drag::Right:
        box.width = std::max(8.0, press_box_.width + along);
        shift((box.width - press_box_.width) / 2.0, 0.0);
        break;
      case Drag::Top:
        box.height = std::max(8.0, press_box_.height - across);
        shift(0.0, (press_box_.height - box.height) / 2.0);
        break;
      case Drag::Bottom:
        box.height = std::max(8.0, press_box_.height + across);
        shift(0.0, (box.height - press_box_.height) / 2.0);
        break;
      case Drag::LineA:
        line_a_ = std::clamp(press_line_a_ + along / press_box_.width, 0.0, 1.0);
        break;
      case Drag::LineB:
        line_b_ = std::clamp(press_line_b_ + along / press_box_.width, 0.0, 1.0);
        break;
      case Drag::LineEither:
      case Drag::None:
        break;
    }
    if (drag_ != Drag::LineA && drag_ != Drag::LineB) {
      box_ = box;
    }
    update();
  }
  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && drag_ != Drag::None) {
      drag_ = Drag::None;
      // A spine line dragged across the other one: they stay ordered left to right. A
      // spread has one gutter line (line_b_ only mirrors its old place), so never swap there.
      if (kind_ == Kind::Cover && line_a_ > line_b_) {
        std::swap(line_a_, line_b_);
      }
      if (on_edited) {
        on_edited();
      }
    }
  }

private:
  enum class Drag { None, Move, Left, Right, Top, Bottom, LineA, LineB, LineEither };

  void draw_label(QPainter& painter, double fraction, const QString& text) const {
    if (text.isEmpty()) {
      return;
    }
    const double radians = box_.angle_degrees * kPi / 180.0;
    const double local_x = (fraction - 0.5) * box_.width;
    const QPointF at = to_widget({box_.center_x + local_x * std::cos(radians), box_.center_y + local_x * std::sin(radians)});
    QRectF plate = QFontMetricsF(painter.font()).boundingRect(text).adjusted(-6.0, -3.0, 6.0, 3.0);
    plate.moveCenter(at);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 170));  // color-literal-ok: label plate over scan content
    painter.drawRoundedRect(plate, 4.0, 4.0);
    painter.setPen(QColor(255, 255, 255));  // color-literal-ok: label text over scan content
    painter.drawText(plate, Qt::AlignCenter, text);
    painter.setBrush(Qt::NoBrush);
  }

  [[nodiscard]] QRectF image_rect() const {
    const QSizeF scaled = QSizeF(full_size_).scaled(QSizeF(size()), Qt::KeepAspectRatio);
    return {(width() - scaled.width()) / 2.0, (height() - scaled.height()) / 2.0, scaled.width(), scaled.height()};
  }
  [[nodiscard]] double view_scale() const {
    return full_size_.width() > 0 ? image_rect().width() / full_size_.width() : 1.0;
  }
  [[nodiscard]] QPointF to_widget(QPointF image) const {
    const QRectF target = image_rect();
    return target.topLeft() + image * view_scale();
  }
  [[nodiscard]] QPointF to_image(QPointF widget) const {
    const double scale = view_scale();
    return scale > 0.0 ? (widget - image_rect().topLeft()) / scale : QPointF();
  }
  [[nodiscard]] std::pair<QPointF, QPointF> line_ends(double fraction) const {
    const double radians = box_.angle_degrees * kPi / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const double local_x = (fraction - 0.5) * box_.width;
    const double half = box_.height / 2.0;
    return {{box_.center_x + local_x * c + half * s, box_.center_y + local_x * s - half * c},
            {box_.center_x + local_x * c - half * s, box_.center_y + local_x * s + half * c}};
  }
  [[nodiscard]] Drag hit_test(QPointF widget) const {
    const QPointF point = to_image(widget);
    const double tolerance = 8.0 / std::max(view_scale(), 1e-6);
    const double radians = box_.angle_degrees * kPi / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const double dx = point.x() - box_.center_x;
    const double dy = point.y() - box_.center_y;
    const double local_x = dx * c + dy * s;
    const double local_y = -dx * s + dy * c;
    const double half_w = box_.width / 2.0;
    const double half_h = box_.height / 2.0;
    const bool within_y = std::abs(local_y) <= half_h + tolerance;
    const bool within_x = std::abs(local_x) <= half_w + tolerance;
    if (kind_ != Kind::Page && within_y) {
      const double to_a = std::abs(local_x - (line_a_ - 0.5) * box_.width);
      const double to_b = kind_ == Kind::Cover ? std::abs(local_x - (line_b_ - 0.5) * box_.width) : tolerance * 2.0;
      if (to_a <= tolerance || to_b <= tolerance) {
        // The nearer line; lines on top of each other wait for the drag direction.
        if (std::abs(to_a - to_b) < 1.0) {
          return Drag::LineEither;
        }
        return to_a < to_b ? Drag::LineA : Drag::LineB;
      }
    }
    if (within_y && std::abs(local_x + half_w) <= tolerance) {
      return Drag::Left;
    }
    if (within_y && std::abs(local_x - half_w) <= tolerance) {
      return Drag::Right;
    }
    if (within_x && std::abs(local_y + half_h) <= tolerance) {
      return Drag::Top;
    }
    if (within_x && std::abs(local_y - half_h) <= tolerance) {
      return Drag::Bottom;
    }
    if (std::abs(local_x) < half_w && std::abs(local_y) < half_h) {
      return Drag::Move;
    }
    return Drag::None;
  }

  Kind kind_{Kind::Spread};
  QImage display_;
  QSize full_size_;
  OrientedBox box_;
  double line_a_{0.5};
  double line_b_{0.5};
  QString left_label_;
  QString right_label_;
  Drag drag_{Drag::None};
  QPointF press_;
  OrientedBox press_box_;
  double press_line_a_{0.5};
  double press_line_b_{0.5};
};

// The review step's page thumbnails. A drag moves the selected pages to the gap under
// the drop, shown as a bar; the items keep their grid places (an icon view would
// otherwise just move icons around without changing the order).
class BookPagesList final : public QListWidget {
public:
  static constexpr const char* kMimeType = "application/x-patchy-book-pages";

  explicit BookPagesList(QWidget* parent) : QListWidget(parent) {
    setViewMode(QListView::IconMode);
    // Static first: it turns dragging and the viewport's drops off; InternalMove turns
    // dragging back on, but drops only on the list itself, not its viewport.
    setMovement(QListView::Static);
    setDragDropMode(QAbstractItemView::InternalMove);
    viewport()->setAcceptDrops(true);
    setDefaultDropAction(Qt::MoveAction);
  }

  // Rows in the order shown, and the row they go before (count() for the end).
  std::function<void(const std::vector<int>&, int)> on_move;

  // What a drag of `rows` carries; a drop is taken only from this list.
  [[nodiscard]] QByteArray drag_payload(const std::vector<int>& rows) const {
    QStringList parts;
    for (const int row : rows) {
      parts << QString::number(row);
    }
    return QByteArray::number(reinterpret_cast<quintptr>(this)) + ':' + parts.join(QLatin1Char(',')).toLatin1();
  }

  // The gap a drop at `pos` (viewport coordinates) falls into: before the nearest page
  // when left of its middle, after it otherwise.
  [[nodiscard]] int insertion_row(QPoint pos) const {
    const int rows = count();
    int best = -1;
    qint64 best_score = std::numeric_limits<qint64>::max();
    for (int row = 0; row < rows; ++row) {
      const QRect rect = visualItemRect(item(row));
      const int dy = pos.y() < rect.top() ? rect.top() - pos.y() : pos.y() > rect.bottom() ? pos.y() - rect.bottom() : 0;
      const qint64 score = static_cast<qint64>(dy) * 100000 + std::abs(pos.x() - rect.center().x());
      if (score < best_score) {
        best_score = score;
        best = row;
      }
    }
    if (best < 0) {
      return 0;
    }
    return pos.x() < visualItemRect(item(best)).center().x() ? best : best + 1;
  }

protected:
  void startDrag(Qt::DropActions) override {
    std::vector<int> rows;
    for (int row = 0; row < count(); ++row) {
      if (item(row)->isSelected()) {
        rows.push_back(row);
      }
    }
    if (rows.empty()) {
      return;
    }
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kMimeType), drag_payload(rows));
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    QListWidgetItem* lead = currentItem() != nullptr && currentItem()->isSelected() ? currentItem() : item(rows.front());
    const QPixmap pixmap = lead->icon().pixmap(iconSize());
    if (!pixmap.isNull()) {
      drag->setPixmap(pixmap);
      drag->setHotSpot(QPoint(pixmap.width() / 2, pixmap.height() / 2));
    }
    drag->exec(Qt::MoveAction, Qt::MoveAction);
    end_drag();
  }
  void dragEnterEvent(QDragEnterEvent* event) override {
    if (!dragged_rows(event->mimeData())) {
      event->ignore();
      return;
    }
    setState(DraggingState);
    event->setDropAction(Qt::MoveAction);
    event->accept();
  }
  void dragMoveEvent(QDragMoveEvent* event) override {
    if (!dragged_rows(event->mimeData())) {
      event->ignore();
      return;
    }
    const int row = insertion_row(event->position().toPoint());
    if (row != drop_row_) {
      drop_row_ = row;
      viewport()->update();
    }
    startAutoScroll();
    event->setDropAction(Qt::MoveAction);
    event->accept();
  }
  void dragLeaveEvent(QDragLeaveEvent*) override { end_drag(); }
  void dropEvent(QDropEvent* event) override {
    const auto rows = dragged_rows(event->mimeData());
    if (!rows) {
      event->ignore();
      return;
    }
    const int before = insertion_row(event->position().toPoint());
    end_drag();
    // The rows are moved here; the drag must not remove anything afterwards.
    event->setDropAction(Qt::MoveAction);
    event->accept();
    if (on_move) {
      on_move(*rows, before);
    }
  }
  void paintEvent(QPaintEvent* event) override {
    QListWidget::paintEvent(event);
    if (drop_row_ < 0 || count() == 0) {
      return;
    }
    const bool at_end = drop_row_ >= count();
    const QRect rect = visualItemRect(item(at_end ? count() - 1 : drop_row_));
    const int gap = std::max(2, spacing());
    const int x = at_end ? rect.right() + gap / 2 + 1 : rect.left() - gap / 2;
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    QColor halo = theme().drop_indicator_border;
    halo.setAlpha(180);
    painter.setPen(QPen(halo, 5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPoint(x, rect.top() + 2), QPoint(x, rect.bottom() - 2));
    painter.setPen(QPen(theme().drop_indicator_bg, 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPoint(x, rect.top() + 2), QPoint(x, rect.bottom() - 2));
  }

private:
  [[nodiscard]] std::optional<std::vector<int>> dragged_rows(const QMimeData* mime) const {
    if (mime == nullptr || !mime->hasFormat(QString::fromLatin1(kMimeType))) {
      return std::nullopt;
    }
    const QByteArray payload = mime->data(QString::fromLatin1(kMimeType));
    const qsizetype colon = payload.indexOf(':');
    if (colon < 0 || payload.left(colon) != QByteArray::number(reinterpret_cast<quintptr>(this))) {
      return std::nullopt;
    }
    std::vector<int> rows;
    for (const QByteArray& part : payload.mid(colon + 1).split(',')) {
      bool ok = false;
      const int row = part.toInt(&ok);
      if (ok && row >= 0 && row < count()) {
        rows.push_back(row);
      }
    }
    if (rows.empty()) {
      return std::nullopt;
    }
    return rows;
  }
  void end_drag() {
    stopAutoScroll();
    setState(NoState);
    if (drop_row_ >= 0) {
      drop_row_ = -1;
      viewport()->update();
    }
  }

  int drop_row_{-1};
};

BookScanDialog::BookScanDialog(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("bookScanDialog"));
  setWindowTitle(tr("Scan a Book to PDF"));
  setModal(false);
  resize(980, 720);
  QSettings settings = app_settings();
  project_.capture_dpi = settings.value(QStringLiteral("bookScan/dpi"), 300).toInt();
  project_.capture_mode = settings.value(QStringLiteral("bookScan/colorMode")).toString() == QLatin1String("gray")
                              ? ScanColorMode::Grayscale
                              : ScanColorMode::Color;
  project_.direction = settings.value(QStringLiteral("bookScan/readingDirection")).toString() == QLatin1String("rtl")
                           ? BookReadingDirection::RightToLeft
                           : BookReadingDirection::LeftToRight;
  project_.include_covers = settings.value(QStringLiteral("bookScan/includeCovers"), true).toBool();
  project_.scan_book_area = settings.value(QStringLiteral("bookScan/scanArea")).toString() != QLatin1String("glass");
  project_.device_id = settings.value(QStringLiteral("bookScan/deviceId")).toString();
  session_ = create_scanner_session(this);

  auto* root = new QVBoxLayout(this);
  step_title_ = new QLabel(this);
  step_title_->setObjectName(QStringLiteral("bookScanStepTitle"));
  QFont title_font = step_title_->font();
  title_font.setBold(true);
  title_font.setPointSizeF(title_font.pointSizeF() * 1.2);
  step_title_->setFont(title_font);
  root->addWidget(step_title_);
  stack_ = new QStackedWidget(this);
  root->addWidget(stack_, 1);
  build_setup_page();
  build_cover_page();
  build_scanning_page();
  build_review_page();
  auto* close_row = new QHBoxLayout;
  close_row->addStretch(1);
  auto* close_button = new QPushButton(tr("Close"), this);
  close_button->setObjectName(QStringLiteral("bookScanCloseButton"));
  close_button->setAutoDefault(false);
  connect(close_button, &QPushButton::clicked, this, &QDialog::reject);
  close_row->addWidget(close_button);
  root->addLayout(close_row);

  connect_session();
  refresh_devices();
  go_to(Step::Setup);
  // The last book's folder: the setup step then offers to continue that book.
  folder_edit_->setText(QDir::toNativeSeparators(default_project_folder()));
  refresh_folder_state();
}

BookScanDialog::~BookScanDialog() = default;

QString BookScanDialog::default_project_folder() const {
  const QString last = app_settings().value(QStringLiteral("bookScan/lastFolder")).toString();
  if (!last.isEmpty()) {
    return last;
  }
  return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(tr("Patchy Book Scans"));
}

void BookScanDialog::build_setup_page() {
  auto* page = new QWidget(stack_);
  page->setObjectName(QStringLiteral("bookScanSetupPage"));
  auto* layout = new QVBoxLayout(page);
  auto* form = new QFormLayout;
  title_edit_ = new QLineEdit(page);
  title_edit_->setObjectName(QStringLiteral("bookScanTitleEdit"));
  author_edit_ = new QLineEdit(page);
  author_edit_->setObjectName(QStringLiteral("bookScanAuthorEdit"));
  form->addRow(tr("Title:"), title_edit_);
  form->addRow(tr("Author:"), author_edit_);
  auto* folder_row = new QHBoxLayout;
  folder_edit_ = new QLineEdit(page);
  folder_edit_->setObjectName(QStringLiteral("bookScanFolderEdit"));
  auto* browse = new QPushButton(tr("Browse..."), page);
  browse->setAutoDefault(false);
  folder_row->addWidget(folder_edit_, 1);
  folder_row->addWidget(browse);
  form->addRow(tr("Project folder:"), folder_row);
  auto* device_row = new QHBoxLayout;
  device_combo_ = new QComboBox(page);
  device_combo_->setObjectName(QStringLiteral("bookScanDeviceCombo"));
  auto* refresh = new QPushButton(tr("Refresh"), page);
  refresh->setAutoDefault(false);
  device_row->addWidget(device_combo_, 1);
  device_row->addWidget(refresh);
  form->addRow(tr("Scanner:"), device_row);
  system_dialog_check_ = new QCheckBox(tr("Use the system scan dialog for each scan"), page);
  system_dialog_check_->setObjectName(QStringLiteral("bookScanSystemDialogCheck"));
  system_dialog_check_->setToolTip(
      tr("For scanners Patchy cannot control directly. You then choose the resolution and mode in the dialog each time."));
  form->addRow(QString(), system_dialog_check_);
  layout->addLayout(form);
  disk_label_ = new QLabel(page);
  disk_label_->setObjectName(QStringLiteral("bookScanDiskLabel"));
  layout->addWidget(disk_label_);
  folder_status_label_ = new QLabel(page);
  folder_status_label_->setObjectName(QStringLiteral("bookScanFolderStatus"));
  folder_status_label_->setWordWrap(true);
  QFont status_font = folder_status_label_->font();
  status_font.setBold(true);
  folder_status_label_->setFont(status_font);
  layout->addWidget(folder_status_label_);
  auto* instructions = new QLabel(
      tr("Open the book and lay it face down on the glass so the back cover, the spine and the front cover are all "
         "on it, then press Scan Cover. Patchy measures the book from this scan. Skip it to measure from the first "
         "pages instead (the covers are then not in the PDF)."),
      page);
  instructions->setWordWrap(true);
  setup_instructions_ = instructions;
  layout->addWidget(instructions);
  layout->addStretch(1);
  auto* buttons = new QHBoxLayout;
  start_over_button_ = new QPushButton(tr("Start Over..."), page);
  start_over_button_->setObjectName(QStringLiteral("bookScanStartOverButton"));
  start_over_button_->setAutoDefault(false);
  start_over_button_->setToolTip(tr("Delete this folder's book scans and scan a new book here"));
  buttons->addWidget(start_over_button_);
  buttons->addStretch(1);
  skip_cover_button_ = new QPushButton(tr("Skip Cover"), page);
  skip_cover_button_->setObjectName(QStringLiteral("bookScanSkipCoverButton"));
  skip_cover_button_->setAutoDefault(false);
  scan_cover_button_ = new QPushButton(tr("Scan Cover"), page);
  scan_cover_button_->setObjectName(QStringLiteral("bookScanCoverButton"));
  scan_cover_button_->setDefault(true);
  continue_button_ = new QPushButton(tr("Continue This Book"), page);
  continue_button_->setObjectName(QStringLiteral("bookScanContinueButton"));
  buttons->addWidget(skip_cover_button_);
  buttons->addWidget(scan_cover_button_);
  buttons->addWidget(continue_button_);
  layout->addLayout(buttons);
  stack_->addWidget(page);

  connect(browse, &QPushButton::clicked, this, [this] {
    const QString chosen = QFileDialog::getExistingDirectory(this, tr("Choose Project Folder"),
                                                             QFileInfo(folder_edit_->text()).absolutePath());
    if (!chosen.isEmpty()) {
      folder_edit_->setText(QDir::toNativeSeparators(chosen));
    }
  });
  connect(title_edit_, &QLineEdit::textEdited, this, [this] { title_from_folder_book_ = false; });
  connect(author_edit_, &QLineEdit::textEdited, this, [this] { title_from_folder_book_ = false; });
  connect(folder_edit_, &QLineEdit::textChanged, this, [this] {
    refresh_folder_state();
    const QStorageInfo storage(QFileInfo(QDir::fromNativeSeparators(folder_edit_->text())).absolutePath());
    if (!storage.isValid()) {
      disk_label_->clear();
      return;
    }
    const qint64 free = storage.bytesAvailable();
    QString text = tr("Free space: %1 GB. A color book at 300 dpi needs about 2 to 4 GB of scans.")
                       .arg(QLocale().toString(static_cast<double>(free) / (1024.0 * 1024.0 * 1024.0), 'f', 1));
    if (free >= 0 && free < kLowDiskBytes) {
      text = tr("Low disk space: %1 GB free. A color book at 300 dpi needs about 2 to 4 GB of scans.")
                 .arg(QLocale().toString(static_cast<double>(free) / (1024.0 * 1024.0 * 1024.0), 'f', 1));
    }
    disk_label_->setText(text);
  });
  connect(refresh, &QPushButton::clicked, this, &BookScanDialog::refresh_devices);
  connect(system_dialog_check_, &QCheckBox::toggled, this, [this](bool on) {
    session_ = on ? create_dialog_scanner_session(this) : create_scanner_session(this);
    session_open_ = false;
    drop_after_scan_ = false;
    connect_session();
    refresh_devices();
  });
  // Another scanner picked after one was opened: open the new one on the next scan.
  connect(device_combo_, &QComboBox::currentIndexChanged, this, [this] {
    if (session_open_ && !scan_in_progress() && device_combo_->currentData().toString() != project_.device_id) {
      drop_session();
    }
  });
  connect(scan_cover_button_, &QPushButton::clicked, this, &BookScanDialog::start_cover_scan);
  connect(skip_cover_button_, &QPushButton::clicked, this, &BookScanDialog::skip_cover);
  connect(continue_button_, &QPushButton::clicked, this, &BookScanDialog::continue_book);
  connect(start_over_button_, &QPushButton::clicked, this, &BookScanDialog::start_over);
}

void BookScanDialog::build_cover_page() {
  auto* page = new QWidget(stack_);
  page->setObjectName(QStringLiteral("bookScanCoverPage"));
  auto* layout = new QHBoxLayout(page);
  cover_preview_ = new BookScanPreview(page);
  cover_preview_->setObjectName(QStringLiteral("bookScanCoverPreview"));
  cover_preview_->set_kind(BookScanPreview::Kind::Cover);
  layout->addWidget(cover_preview_, 1);
  auto* side = new QVBoxLayout;
  auto* hint = new QLabel(tr("Check the outline of the book and drag the two spine lines onto the spine's edges."), page);
  hint->setWordWrap(true);
  side->addWidget(hint);
  auto* form = new QFormLayout;
  up_combo_ = new QComboBox(page);
  up_combo_->setObjectName(QStringLiteral("bookScanUpCombo"));
  up_combo_->addItems({tr("Up"), tr("Right"), tr("Down"), tr("Left")});
  up_combo_->setToolTip(tr("Which way the top of the book points in the scan"));
  form->addRow(tr("Top of the book points:"), up_combo_);
  direction_combo_ = new QComboBox(page);
  direction_combo_->setObjectName(QStringLiteral("bookScanDirectionCombo"));
  direction_combo_->addItems({tr("Left to right"), tr("Right to left")});
  form->addRow(tr("Reading direction:"), direction_combo_);
  cover_tilt_spin_ = new QDoubleSpinBox(page);
  cover_tilt_spin_->setObjectName(QStringLiteral("bookScanCoverTiltSpin"));
  cover_tilt_spin_->setRange(-45.0, 45.0);
  cover_tilt_spin_->setDecimals(1);
  cover_tilt_spin_->setSuffix(tr(" deg"));
  form->addRow(tr("Tilt:"), cover_tilt_spin_);
  page_size_label_ = new QLabel(page);
  page_size_label_->setObjectName(QStringLiteral("bookScanPageSizeLabel"));
  form->addRow(tr("Page size:"), page_size_label_);
  side->addLayout(form);
  auto* capture = new QGroupBox(tr("Page scans"), page);
  auto* capture_form = new QFormLayout(capture);
  resolution_combo_ = new QComboBox(capture);
  resolution_combo_->setObjectName(QStringLiteral("bookScanResolutionCombo"));
  capture_form->addRow(tr("Resolution:"), resolution_combo_);
  color_combo_ = new QComboBox(capture);
  color_combo_->setObjectName(QStringLiteral("bookScanColorCombo"));
  color_combo_->addItems({tr("Color"), tr("Grayscale")});
  capture_form->addRow(tr("Mode:"), color_combo_);
  area_combo_ = new QComboBox(capture);
  area_combo_->setObjectName(QStringLiteral("bookScanAreaCombo"));
  area_combo_->addItems({tr("Book area (faster)"), tr("Whole glass")});
  capture_form->addRow(tr("Scan area:"), area_combo_);
  mode_combo_ = new QComboBox(capture);
  mode_combo_->setObjectName(QStringLiteral("bookScanModeCombo"));
  mode_combo_->addItems({tr("Two pages per scan"), tr("One page per scan")});
  capture_form->addRow(tr("Pages:"), mode_combo_);
  mode_notice_ = new QLabel(capture);
  mode_notice_->setObjectName(QStringLiteral("bookScanModeNotice"));
  mode_notice_->setWordWrap(true);
  capture_form->addRow(mode_notice_);
  covers_check_ = new QCheckBox(tr("Include the covers in the PDF"), capture);
  covers_check_->setObjectName(QStringLiteral("bookScanCoversCheck"));
  capture_form->addRow(covers_check_);
  side->addWidget(capture);
  side->addStretch(1);
  auto* buttons = new QHBoxLayout;
  auto* rescan = new QPushButton(tr("Rescan Cover"), page);
  rescan->setObjectName(QStringLiteral("bookScanRescanCoverButton"));
  rescan->setAutoDefault(false);
  auto* start = new QPushButton(tr("Start Scanning Pages"), page);
  start->setObjectName(QStringLiteral("bookScanStartPagesButton"));
  start->setDefault(true);
  buttons->addWidget(rescan);
  buttons->addStretch(1);
  buttons->addWidget(start);
  side->addLayout(buttons);
  layout->addLayout(side);
  stack_->addWidget(page);

  cover_preview_->on_edited = [this] {
    if (!project_.cover) {
      return;
    }
    project_.cover->layout.outline = cover_preview_->box();
    project_.cover->layout.spine_left = cover_preview_->line_a();
    project_.cover->layout.spine_right = cover_preview_->line_b();
    project_.cover->user_edited = true;
    cover_settings_changed();
  };
  connect(up_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (refreshing_ || !project_.cover) {
      return;
    }
    set_up_turns((4 - index) % 4);
    refresh_cover_page();
    cover_settings_changed();
  });
  connect(direction_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (!refreshing_) {
      project_.direction = index == 1 ? BookReadingDirection::RightToLeft : BookReadingDirection::LeftToRight;
      update_cover_labels();
      cover_settings_changed();
    }
  });
  connect(cover_tilt_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (!refreshing_ && project_.cover) {
      project_.cover->layout.outline.angle_degrees = value;
      project_.cover->user_edited = true;
      cover_preview_->set_box(project_.cover->layout.outline);
      cover_settings_changed();
    }
  });
  for (QComboBox* combo : {resolution_combo_, color_combo_, area_combo_, mode_combo_}) {
    connect(combo, &QComboBox::currentIndexChanged, this, [this] {
      if (!refreshing_) {
        cover_settings_changed();
      }
    });
  }
  connect(covers_check_, &QCheckBox::toggled, this, [this] {
    if (!refreshing_) {
      cover_settings_changed();
    }
  });
  connect(rescan, &QPushButton::clicked, this, &BookScanDialog::start_cover_scan);
  connect(start, &QPushButton::clicked, this, &BookScanDialog::start_page_scanning);
}

void BookScanDialog::build_scanning_page() {
  auto* page = new QWidget(stack_);
  page->setObjectName(QStringLiteral("bookScanScanningPage"));
  auto* layout = new QVBoxLayout(page);
  auto* top = new QHBoxLayout;
  spread_preview_ = new BookScanPreview(page);
  spread_preview_->setObjectName(QStringLiteral("bookScanSpreadPreview"));
  top->addWidget(spread_preview_, 1);
  auto* side = new QVBoxLayout;
  instruction_label_ = new QLabel(page);
  instruction_label_->setObjectName(QStringLiteral("bookScanInstructionLabel"));
  instruction_label_->setWordWrap(true);
  QFont instruction_font = instruction_label_->font();
  instruction_font.setPointSizeF(instruction_font.pointSizeF() * 1.15);
  instruction_label_->setFont(instruction_font);
  side->addWidget(instruction_label_);
  status_label_ = new QLabel(page);
  status_label_->setObjectName(QStringLiteral("bookScanStatusLabel"));
  status_label_->setWordWrap(true);
  side->addWidget(status_label_);
  progress_bar_ = new QProgressBar(page);
  progress_bar_->setObjectName(QStringLiteral("bookScanProgressBar"));
  progress_bar_->setVisible(false);
  side->addWidget(progress_bar_);
  edge_warning_ = new QLabel(tr("The book may reach past the scanned area. Rescan with Whole glass if a page is cut off."),
                             page);
  edge_warning_->setObjectName(QStringLiteral("bookScanEdgeWarning"));
  edge_warning_->setWordWrap(true);
  edge_warning_->setVisible(false);
  side->addWidget(edge_warning_);
  grayscale_hint_ = new QLabel(tr("These pages look grayscale. Scanning in Grayscale is faster and makes a smaller PDF."),
                               page);
  grayscale_hint_->setObjectName(QStringLiteral("bookScanGrayscaleHint"));
  grayscale_hint_->setWordWrap(true);
  grayscale_hint_->setVisible(false);
  side->addWidget(grayscale_hint_);
  grayscale_switch_ = new QPushButton(tr("Switch to Grayscale"), page);
  grayscale_switch_->setObjectName(QStringLiteral("bookScanGrayscaleSwitch"));
  grayscale_switch_->setAutoDefault(false);
  grayscale_switch_->setVisible(false);
  side->addWidget(grayscale_switch_);
  auto* form = new QFormLayout;
  scan_form_ = form;
  pages_combo_ = new QComboBox(page);
  pages_combo_->setObjectName(QStringLiteral("bookScanPagesCombo"));
  pages_combo_->addItems({tr("Two pages per scan"), tr("One page per scan")});
  form->addRow(tr("Pages:"), pages_combo_);
  use_combo_ = new QComboBox(page);
  use_combo_->setObjectName(QStringLiteral("bookScanUseCombo"));
  use_combo_->addItems({tr("Both pages"), tr("Left page only"), tr("Right page only")});
  use_combo_->setToolTip(tr("Leave out a blank inside cover or the empty side of the last spread"));
  form->addRow(tr("Use:"), use_combo_);
  // The cover's own option, shown while the cover is selected for editing.
  scan_up_combo_ = new QComboBox(page);
  scan_up_combo_->setObjectName(QStringLiteral("bookScanScanningUpCombo"));
  scan_up_combo_->addItems({tr("Up"), tr("Right"), tr("Down"), tr("Left")});
  scan_up_combo_->setToolTip(tr("Which way the top of the book points in the scan"));
  form->addRow(tr("Top of the book points:"), scan_up_combo_);
  spread_tilt_spin_ = new QDoubleSpinBox(page);
  spread_tilt_spin_->setObjectName(QStringLiteral("bookScanSpreadTiltSpin"));
  spread_tilt_spin_->setRange(-45.0, 45.0);
  spread_tilt_spin_->setDecimals(1);
  spread_tilt_spin_->setSuffix(tr(" deg"));
  form->addRow(tr("Tilt:"), spread_tilt_spin_);
  // Capture settings can change between scans: a few pages in color, a plate at a higher
  // resolution. With an earlier scan selected they are what Rescan This Scan uses.
  scan_resolution_combo_ = new QComboBox(page);
  scan_resolution_combo_->setObjectName(QStringLiteral("bookScanScanningResolutionCombo"));
  scan_resolution_combo_->setToolTip(tr("The resolution of the next scans. Earlier scans keep their own."));
  form->addRow(tr("Resolution:"), scan_resolution_combo_);
  scan_color_combo_ = new QComboBox(page);
  scan_color_combo_->setObjectName(QStringLiteral("bookScanScanningColorCombo"));
  scan_color_combo_->addItems({tr("Color"), tr("Grayscale")});
  scan_color_combo_->setToolTip(tr("Scan the next pages in color or grayscale. Earlier scans keep their own."));
  form->addRow(tr("Mode:"), scan_color_combo_);
  side->addLayout(form);
  capture_note_ = new QLabel(page);
  capture_note_->setObjectName(QStringLiteral("bookScanCaptureNote"));
  capture_note_->setWordWrap(true);
  side->addWidget(capture_note_);
  auto* rotate = new QPushButton(tr("Turn This Scan 180 Degrees"), page);
  rotate->setObjectName(QStringLiteral("bookScanRotateSpreadButton"));
  rotate->setAutoDefault(false);
  rotate_scan_button_ = rotate;
  side->addWidget(rotate);
  beep_check_ = new QCheckBox(tr("Beep when a scan finishes"), page);
  beep_check_->setObjectName(QStringLiteral("bookScanBeepCheck"));
  beep_check_->setChecked(app_settings().value(QStringLiteral("bookScan/beep"), true).toBool());
  side->addWidget(beep_check_);
  side->addStretch(1);
  top->addLayout(side);
  layout->addLayout(top, 1);
  strip_ = new QListWidget(page);
  strip_->setObjectName(QStringLiteral("bookScanPageStrip"));
  strip_->setViewMode(QListView::IconMode);
  strip_->setFlow(QListView::LeftToRight);
  strip_->setWrapping(false);
  strip_->setIconSize(QSize(64, 90));
  // Each item is as wide as its label; a selected one would otherwise lose the end of it.
  strip_->setTextElideMode(Qt::ElideNone);
  strip_->setFixedHeight(150);
  strip_->setMovement(QListView::Static);
  strip_->setFocusPolicy(Qt::NoFocus);
  // The selected scan's pages are highlighted; a click picks another scan (refresh rebuilds).
  strip_->setSelectionMode(QAbstractItemView::MultiSelection);
  strip_->setToolTip(tr("Click a page to adjust or rescan its scan"));
  layout->addWidget(strip_);
  auto* buttons = new QHBoxLayout;
  rescan_button_ = new QPushButton(tr("Rescan Last"), page);
  rescan_button_->setObjectName(QStringLiteral("bookScanRescanButton"));
  rescan_button_->setAutoDefault(false);
  cancel_scan_button_ = new QPushButton(tr("Cancel Scan"), page);
  cancel_scan_button_->setObjectName(QStringLiteral("bookScanCancelButton"));
  cancel_scan_button_->setAutoDefault(false);
  cancel_scan_button_->setVisible(false);
  back_to_next_button_ = new QPushButton(tr("Back to Next Pages"), page);
  back_to_next_button_->setObjectName(QStringLiteral("bookScanBackToNextButton"));
  back_to_next_button_->setAutoDefault(false);
  back_to_next_button_->setVisible(false);
  finished_button_ = new QPushButton(tr("Finished"), page);
  finished_button_->setObjectName(QStringLiteral("bookScanFinishedButton"));
  finished_button_->setAutoDefault(false);
  scan_button_ = new QPushButton(tr("Scan"), page);
  scan_button_->setObjectName(QStringLiteral("bookScanScanButton"));
  scan_button_->setDefault(true);
  scan_button_->setMinimumWidth(140);
  buttons->addWidget(rescan_button_);
  buttons->addWidget(cancel_scan_button_);
  buttons->addWidget(back_to_next_button_);
  buttons->addStretch(1);
  buttons->addWidget(finished_button_);
  buttons->addWidget(scan_button_);
  layout->addLayout(buttons);
  stack_->addWidget(page);

  // Space scans too, so the user never reaches for the mouse while handling the book.
  auto* space = new QShortcut(QKeySequence(Qt::Key_Space), page);
  space->setContext(Qt::WidgetWithChildrenShortcut);
  connect(space, &QShortcut::activated, this, [this] {
    if (step_ == Step::Scanning && scan_button_->isEnabled()) {
      start_page_scan();
    }
  });
  spread_preview_->on_edited = [this] {
    if (edit_cover_) {
      if (!project_.cover) {
        return;
      }
      project_.cover->layout.outline = spread_preview_->box();
      project_.cover->layout.spine_left = spread_preview_->line_a();
      project_.cover->layout.spine_right = spread_preview_->line_b();
      project_.cover->user_edited = true;
      update_book_area();
      thumbnails_.clear();
      save_project_quietly();
      refresh_scanning_page();
      return;
    }
    const int index = selected_scan();
    if (index < 0) {
      return;
    }
    auto& scan = project_.scans[static_cast<std::size_t>(index)];
    scan.layout.spread = spread_preview_->box();
    scan.layout.gutter = spread_preview_->line_a();
    scan.user_edited = true;
    thumbnails_.clear();
    save_project_quietly();
    refresh_scanning_page();
  };
  connect(pages_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (!refreshing_) {
      set_scan_mode(index == 1 ? BookScanMode::OnePage : BookScanMode::TwoPages);
    }
  });
  connect(use_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    const int scan = selected_scan();
    if (refreshing_ || edit_cover_ || scan < 0) {
      return;
    }
    project_.scans[static_cast<std::size_t>(scan)].use =
        index == 1 ? BookPageUse::LeftOnly : index == 2 ? BookPageUse::RightOnly : BookPageUse::Both;
    save_project_quietly();
    refresh_scanning_page();
  });
  connect(scan_up_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (refreshing_ || !project_.cover || scan_in_progress()) {
      return;
    }
    set_up_turns((4 - index) % 4);
    refresh_scanning_page();
  });
  connect(spread_tilt_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (refreshing_) {
      return;
    }
    if (edit_cover_) {
      if (!project_.cover) {
        return;
      }
      project_.cover->layout.outline.angle_degrees = value;
      project_.cover->user_edited = true;
      update_book_area();
    } else if (const int scan = selected_scan(); scan >= 0) {
      project_.scans[static_cast<std::size_t>(scan)].layout.spread.angle_degrees = value;
      project_.scans[static_cast<std::size_t>(scan)].user_edited = true;
    } else {
      return;
    }
    thumbnails_.clear();
    save_project_quietly();
    refresh_scanning_page();
  });
  connect(rotate, &QPushButton::clicked, this, [this] {
    const int index = selected_scan();
    if (edit_cover_ || index < 0 || scan_in_progress()) {
      return;
    }
    auto& scan = project_.scans[static_cast<std::size_t>(index)];
    scan.extra_turns = (scan.extra_turns + 2) % 4;
    const PixelBuffer upright = load_upright_scan(project_, scan.file, project_.up_turns + scan.extra_turns);
    scan.layout = detect_scan_layout(upright, scan.dpi, std::nullopt);
    scan.user_edited = false;
    thumbnails_.clear();
    save_project_quietly();
    refresh_scanning_page();
  });
  for (QComboBox* combo : {scan_resolution_combo_, scan_color_combo_}) {
    connect(combo, &QComboBox::currentIndexChanged, this, [this] {
      if (!refreshing_) {
        capture_controls_changed();
      }
    });
  }
  connect(grayscale_switch_, &QPushButton::clicked, this, [this] {
    project_.capture_mode = ScanColorMode::Grayscale;
    grayscale_hint_->setVisible(false);
    grayscale_switch_->setVisible(false);
    save_project_quietly();
    refresh_capture_controls();
    status_label_->setText(tr("The next pages are scanned in Grayscale."));
  });
  connect(beep_check_, &QCheckBox::toggled, this,
          [](bool on) { app_settings().setValue(QStringLiteral("bookScan/beep"), on); });
  connect(scan_button_, &QPushButton::clicked, this, &BookScanDialog::start_page_scan);
  connect(rescan_button_, &QPushButton::clicked, this, &BookScanDialog::rescan_selected);
  connect(back_to_next_button_, &QPushButton::clicked, this, &BookScanDialog::back_to_next_pages);
  connect(strip_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    const BookPageRef page = page_from_variant(item->data(Qt::UserRole));
    // Later: the strip is rebuilt, which must not happen inside its own click.
    QTimer::singleShot(0, this, [this, page] { edit_page(page); });
  });
  connect(cancel_scan_button_, &QPushButton::clicked, this, [this] {
    if (session_ != nullptr) {
      session_->cancel();
    }
  });
  connect(finished_button_, &QPushButton::clicked, this, &BookScanDialog::finish_scanning);
}

void BookScanDialog::build_review_page() {
  auto* page = new QWidget(stack_);
  page->setObjectName(QStringLiteral("bookScanReviewPage"));
  auto* layout = new QHBoxLayout(page);
  auto* left = new QVBoxLayout;
  auto* pages_list = new BookPagesList(page);
  pages_list_ = pages_list;
  pages_list_->setObjectName(QStringLiteral("bookScanPagesList"));
  pages_list_->setIconSize(QSize(80, 112));
  pages_list_->setResizeMode(QListView::Adjust);
  pages_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  pages_list_->setToolTip(tr("Drag pages to change their order"));
  // Each item is as wide as its label, so a page's own output ("4 (Color)") shows in full.
  pages_list_->setTextElideMode(Qt::ElideNone);
  pages_list_->setSpacing(6);
  left->addWidget(pages_list_, 1);
  auto* list_buttons = new QHBoxLayout;
  auto* remove = new QPushButton(tr("Delete"), page);
  remove->setObjectName(QStringLiteral("bookScanDeletePagesButton"));
  auto* rotate = new QPushButton(tr("Turn 180 Degrees"), page);
  rotate->setObjectName(QStringLiteral("bookScanRotatePagesButton"));
  auto* blank = new QPushButton(tr("Insert Blank Page"), page);
  blank->setObjectName(QStringLiteral("bookScanBlankPageButton"));
  auto* back = new QPushButton(tr("Scan More Pages"), page);
  back->setObjectName(QStringLiteral("bookScanMorePagesButton"));
  edit_page_button_ = new QPushButton(tr("Edit Scan"), page);
  edit_page_button_->setObjectName(QStringLiteral("bookScanEditPageButton"));
  edit_page_button_->setToolTip(tr("Show this page's scan with its boxes, to adjust them or scan it again"));
  for (QPushButton* button : {remove, rotate, blank, edit_page_button_, back}) {
    button->setAutoDefault(false);
    list_buttons->addWidget(button);
  }
  list_buttons->addStretch(1);
  left->addLayout(list_buttons);
  layout->addLayout(left, 1);

  auto* side = new QVBoxLayout;
  review_preview_ = new QLabel(page);
  review_preview_->setObjectName(QStringLiteral("bookScanReviewPreview"));
  review_preview_->setMinimumSize(260, 340);
  review_preview_->setAlignment(Qt::AlignCenter);
  side->addWidget(review_preview_, 1);
  auto* form = new QFormLayout;
  size_combo_ = new QComboBox(page);
  size_combo_->setObjectName(QStringLiteral("bookScanSizeCombo"));
  size_combo_->addItem(tr("Measured (median)"), QStringLiteral("median"));
  size_combo_->addItem(tr("Largest measured page"), QStringLiteral("largest"));
  size_combo_->addItem(tr("From the cover"), QStringLiteral("cover"));
  size_combo_->addItem(tr("Custom"), QStringLiteral("custom"));
  size_combo_->addItem(tr("A4"), QStringLiteral("a4"));
  size_combo_->addItem(tr("A5"), QStringLiteral("a5"));
  size_combo_->addItem(tr("US Letter"), QStringLiteral("letter"));
  size_combo_->addItem(tr("6 x 9 in"), QStringLiteral("6x9"));
  size_combo_->addItem(tr("5.5 x 8.5 in"), QStringLiteral("5.5x8.5"));
  form->addRow(tr("Page size:"), size_combo_);
  auto* custom_row = new QHBoxLayout;
  custom_width_ = new QDoubleSpinBox(page);
  custom_width_->setObjectName(QStringLiteral("bookScanCustomWidth"));
  custom_height_ = new QDoubleSpinBox(page);
  custom_height_->setObjectName(QStringLiteral("bookScanCustomHeight"));
  for (QDoubleSpinBox* spin : {custom_width_, custom_height_}) {
    if (metric_units()) {
      spin->setRange(20.0, 1000.0);
      spin->setDecimals(0);
      spin->setSuffix(tr(" mm"));
    } else {
      spin->setRange(1.0, 40.0);
      spin->setDecimals(2);
      spin->setSuffix(tr(" in"));
    }
  }
  custom_row->addWidget(custom_width_);
  custom_row->addWidget(new QLabel(QStringLiteral("x"), page));
  custom_row->addWidget(custom_height_);
  form->addRow(QString(), custom_row);
  output_combo_ = new QComboBox(page);
  output_combo_->setObjectName(QStringLiteral("bookScanOutputCombo"));
  output_combo_->addItems({tr("Color"), tr("Grayscale"), tr("Black and white")});
  form->addRow(tr("Output:"), output_combo_);
  page_output_combo_ = new QComboBox(page);
  page_output_combo_->setObjectName(QStringLiteral("bookScanPageOutputCombo"));
  page_output_combo_->addItem(tr("Same as the book"), -1);
  page_output_combo_->addItem(tr("Color"), static_cast<int>(BookOutputMode::Color));
  page_output_combo_->addItem(tr("Grayscale"), static_cast<int>(BookOutputMode::Grayscale));
  page_output_combo_->addItem(tr("Black and white"), static_cast<int>(BookOutputMode::BlackAndWhite));
  page_output_combo_->setToolTip(tr("Give the selected pages their own output, such as color plates in a black and "
                                    "white book"));
  form->addRow(tr("Selected pages:"), page_output_combo_);
  threshold_slider_ = new QSlider(Qt::Horizontal, page);
  threshold_slider_->setObjectName(QStringLiteral("bookScanThresholdSlider"));
  threshold_slider_->setRange(1, 255);
  form->addRow(tr("Threshold:"), threshold_slider_);
  output_dpi_combo_ = new QComboBox(page);
  output_dpi_combo_->setObjectName(QStringLiteral("bookScanOutputDpiCombo"));
  output_dpi_combo_->addItem(tr("As scanned"), 0);
  for (const int dpi : {600, 400, 300, 200, 150}) {
    output_dpi_combo_->addItem(tr("%1 dpi").arg(dpi), dpi);
  }
  form->addRow(tr("Resolution:"), output_dpi_combo_);
  quality_combo_ = new QComboBox(page);
  quality_combo_->setObjectName(QStringLiteral("bookScanQualityCombo"));
  populate_pdf_image_quality_combo(*quality_combo_, project_.quality_id);
  form->addRow(tr("Image quality:"), quality_combo_);
  side->addLayout(form);
  review_summary_ = new QLabel(page);
  review_summary_->setObjectName(QStringLiteral("bookScanReviewSummary"));
  review_summary_->setWordWrap(true);
  side->addWidget(review_summary_);
  auto* buttons = new QHBoxLayout;
  auto* open_folder = new QPushButton(tr("Open Book Folder"), page);
  open_folder->setObjectName(QStringLiteral("bookScanOpenFolderButton"));
  open_folder->setToolTip(tr("Show the scans in the file manager. This window stays open."));
  open_folder->setAutoDefault(false);
  auto* export_button = new QPushButton(tr("Export PDF..."), page);
  export_button->setObjectName(QStringLiteral("bookScanExportButton"));
  export_button->setDefault(true);
  buttons->addWidget(open_folder);
  buttons->addStretch(1);
  buttons->addWidget(export_button);
  side->addLayout(buttons);
  layout->addLayout(side);
  stack_->addWidget(page);

  pages_list->on_move = [this](const std::vector<int>& rows, int before) {
    // Later: the list is rebuilt, which must not happen inside its own drop.
    QTimer::singleShot(0, this, [this, rows, before] { move_review_pages(rows, before); });
  };
  connect(pages_list_, &QListWidget::currentRowChanged, this, &BookScanDialog::refresh_review_preview);
  connect(pages_list_, &QListWidget::itemSelectionChanged, this, &BookScanDialog::update_page_output_combo);
  connect(page_output_combo_, &QComboBox::currentIndexChanged, this, [this] {
    if (!refreshing_) {
      page_output_changed();
    }
  });
  connect(remove, &QPushButton::clicked, this, &BookScanDialog::delete_selected_pages);
  connect(rotate, &QPushButton::clicked, this, &BookScanDialog::rotate_selected_pages);
  connect(blank, &QPushButton::clicked, this, &BookScanDialog::insert_blank_page);
  connect(back, &QPushButton::clicked, this, [this] {
    project_.finished_scanning = false;
    edit_cover_ = false;
    edit_scan_.reset();
    save_project_quietly();
    go_to(Step::Scanning);
  });
  const auto edit_current = [this] {
    if (QListWidgetItem* item = pages_list_->currentItem()) {
      const BookPageRef page = page_from_variant(item->data(Qt::UserRole));
      QTimer::singleShot(0, this, [this, page] { edit_page(page); });
    }
  };
  connect(edit_page_button_, &QPushButton::clicked, this, edit_current);
  connect(pages_list_, &QListWidget::itemDoubleClicked, this, edit_current);
  connect(size_combo_, &QComboBox::currentIndexChanged, this, [this] {
    if (refreshing_) {
      return;
    }
    const QString token = size_combo_->currentData().toString();
    if (token == QLatin1String("median")) {
      project_.size_policy = BookPageSizePolicy::Median;
    } else if (token == QLatin1String("largest")) {
      project_.size_policy = BookPageSizePolicy::Largest;
    } else if (token == QLatin1String("cover")) {
      project_.size_policy = BookPageSizePolicy::Cover;
    } else {
      const BookPageSize current = resolved_page_size(project_);
      project_.size_policy = BookPageSizePolicy::Custom;
      project_.custom_size = current;
      for (const auto& preset : kSizePresets) {
        if (token == QLatin1String(preset.token)) {
          project_.custom_size = {preset.width, preset.height};
        }
      }
    }
    thumbnails_.clear();
    save_project_quietly();
    refresh_review_page();
  });
  const auto custom_changed = [this] {
    if (refreshing_) {
      return;
    }
    const double factor = metric_units() ? 25.4 : 1.0;
    project_.size_policy = BookPageSizePolicy::Custom;
    project_.custom_size = {custom_width_->value() / factor, custom_height_->value() / factor};
    thumbnails_.clear();
    save_project_quietly();
    refresh_review_page();
  };
  connect(custom_width_, &QDoubleSpinBox::editingFinished, this, custom_changed);
  connect(custom_height_, &QDoubleSpinBox::editingFinished, this, custom_changed);
  connect(output_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (refreshing_) {
      return;
    }
    project_.output.mode = index == 1   ? BookOutputMode::Grayscale
                           : index == 2 ? BookOutputMode::BlackAndWhite
                                        : BookOutputMode::Color;
    save_project_quietly();
    refresh_review_page();
  });
  connect(threshold_slider_, &QSlider::valueChanged, this, [this](int value) {
    if (refreshing_) {
      return;
    }
    project_.output.threshold = value;
    refresh_review_preview();
  });
  connect(threshold_slider_, &QSlider::sliderReleased, this, &BookScanDialog::save_project_quietly);
  connect(output_dpi_combo_, &QComboBox::currentIndexChanged, this, [this] {
    if (!refreshing_) {
      project_.output_dpi = output_dpi_combo_->currentData().toInt();
      save_project_quietly();
      refresh_review_page();
    }
  });
  connect(quality_combo_, &QComboBox::currentIndexChanged, this, [this] {
    if (!refreshing_) {
      project_.quality_id = quality_combo_->currentData().toString();
      save_project_quietly();
    }
  });
  connect(open_folder, &QPushButton::clicked, this, &BookScanDialog::open_book_folder);
  connect(export_button, &QPushButton::clicked, this, [this] {
    QString name = project_.title.trimmed().isEmpty() ? tr("Book") : project_.title.trimmed();
    for (const QChar bad : QStringLiteral("\\/:*?\"<>|")) {
      name.replace(bad, QLatin1Char('_'));
    }
    const QString path = get_save_file_name(this, tr("Export Book PDF"), QDir(project_.folder).filePath(name + QStringLiteral(".pdf")),
                                            tr("PDF (*.pdf)"), nullptr, QStringLiteral("bookScanExportFileDialog"));
    if (!path.isEmpty()) {
      export_pdf(path, true);
    }
  });
}

void BookScanDialog::connect_session() {
  if (session_ == nullptr) {
    return;
  }
  connect(session_.get(), &ScannerSession::finished, this, &BookScanDialog::scan_finished);
  connect(session_.get(), &ScannerSession::progress, this, [this](int percent) {
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(percent);
  });
  connect(session_.get(), &ScannerSession::status_message, this,
          [this](const QString& text) { status_label_->setText(text); });
  connect(session_.get(), &ScannerSession::devices_changed, this, &BookScanDialog::devices_changed);
}

void BookScanDialog::refresh_devices() {
  if (device_combo_ == nullptr) {
    return;
  }
  if (session_ == nullptr) {
    device_combo_->clear();
    device_combo_->addItem(tr("Scanning is not available here"));
    scan_cover_button_->setEnabled(false);
    return;
  }
  // Refresh also lets go of the open scanner: one switched off and on since then has a
  // dead handle.
  if (!scan_in_progress()) {
    drop_session();
  }
  apply_devices(session_->devices());
}

void BookScanDialog::apply_devices(const std::vector<ScannerDeviceInfo>& devices) {
  // A book under way keeps its scanner selected; the setup step keeps the user's pick.
  const QString preferred = step_ != Step::Setup && !project_.device_id.isEmpty()
                                ? project_.device_id
                                : device_combo_->currentData().toString();
  {
    const QSignalBlocker blocker(device_combo_);
    device_combo_->clear();
    for (const auto& device : devices) {
      device_combo_->addItem(device.name, device.id);
    }
    if (devices.empty()) {
      device_combo_->addItem(tr("No scanner found"));
    }
    int index = preferred.isEmpty() ? -1 : device_combo_->findData(preferred);
    if (index < 0 && !project_.device_id.isEmpty()) {
      index = device_combo_->findData(project_.device_id);
    }
    if (index >= 0) {
      device_combo_->setCurrentIndex(index);
    }
  }
  const bool found = !devices.empty();
  scan_cover_button_->setEnabled(found && !scan_in_progress());
  skip_cover_button_->setEnabled(found);
  const QString hint = found ? QString() : tr("Switch on or connect a scanner. This list updates when one appears.");
  scan_cover_button_->setToolTip(hint);
  device_combo_->setToolTip(hint);
  // Past the setup step: is the book's scanner still there?
  const bool listed = step_ == Step::Setup || project_.device_id.isEmpty() ||
                      std::any_of(devices.begin(), devices.end(),
                                  [this](const ScannerDeviceInfo& device) { return device.id == project_.device_id; });
  const QString off_text = tr("The scanner is switched off or disconnected. Switch it on to keep scanning.");
  if (!listed) {
    status_label_->setText(off_text);
  } else if (status_label_->text() == off_text) {
    status_label_->clear();
  }
  if (listed != device_listed_) {
    device_listed_ = listed;
    resolution_combo_->setEnabled(capabilities_.direct_control && device_listed_);
    color_combo_->setEnabled(capabilities_.direct_control && device_listed_);
    refresh_capture_controls();
  }
}

void BookScanDialog::devices_changed(const std::vector<ScannerDeviceInfo>& devices) {
  apply_devices(devices);
  // The open scanner's handle may be dead now (switched off and on): open it again on
  // the next scan, which also reads its resolutions and bed size again.
  if (scan_in_progress()) {
    drop_after_scan_ = session_open_;
  } else {
    drop_session();
  }
}

void BookScanDialog::drop_session() {
  if (!session_open_) {
    return;
  }
  session_open_ = false;
  if (session_ != nullptr) {
    session_->close();
  }
}

void BookScanDialog::start_session_scan(const ScanRequest& request) {
  last_request_ = request;
  scan_retried_ = false;
  session_->start_scan(last_request_);
}

bool BookScanDialog::retry_after_device_loss() {
  drop_after_scan_ = false;
  drop_session();
  if (!ensure_session_open(false)) {
    return false;
  }
  scan_retried_ = true;
  ScanRequest request = last_request_;
  if (pending_ != PendingScan::Cover && pending_ != PendingScan::RescanCover) {
    request.dpi = nearest_offered_dpi(request.dpi);  // the reopened scanner may offer other resolutions
  }
  status_label_->setText(tr("Reconnected to the scanner. Scanning again..."));
  session_->start_scan(request);
  return true;
}

void BookScanDialog::go_to(Step step) {
  step_ = step;
  switch (step) {
    case Step::Setup:
      step_title_->setText(tr("1. Set up and scan the cover"));
      stack_->setCurrentIndex(0);
      break;
    case Step::Cover:
      step_title_->setText(tr("2. Check the book"));
      stack_->setCurrentIndex(1);
      refresh_cover_page();
      break;
    case Step::Scanning:
      step_title_->setText(tr("3. Scan the pages"));
      stack_->setCurrentIndex(2);
      refresh_scanning_page();
      scan_button_->setFocus();
      break;
    case Step::Review:
      step_title_->setText(tr("4. Review and export"));
      stack_->setCurrentIndex(3);
      refresh_review_page();
      break;
  }
}

void BookScanDialog::set_project_folder(const QString& folder) {
  folder_edit_->setText(QDir::toNativeSeparators(folder));
}

void BookScanDialog::set_title(const QString& title) {
  title_edit_->setText(title);
  project_.title = title;
}

bool BookScanDialog::ensure_project_folder() {
  if (!project_.folder.isEmpty()) {
    return true;
  }
  const QString folder = QDir::fromNativeSeparators(folder_edit_->text().trimmed());
  if (folder.isEmpty()) {
    show_scan_error(tr("Choose a project folder first."));
    return false;
  }
  const QDir dir(folder);
  if (dir.exists(QString::fromLatin1(kBookProjectFileName))) {
    show_scan_error(tr("That folder already holds a book scan. Continue it, start over, or choose another folder."));
    return false;
  }
  if (!QDir().mkpath(QDir(folder).filePath(scans_dir_name()))) {
    show_scan_error(tr("The project folder %1 could not be created.").arg(QDir::toNativeSeparators(folder)));
    return false;
  }
  project_.folder = QDir(folder).absolutePath();
  project_.title = title_edit_->text().trimmed();
  project_.author = author_edit_->text().trimmed();
  app_settings().setValue(QStringLiteral("bookScan/lastFolder"), QDir::toNativeSeparators(project_.folder));
  return true;
}

void BookScanDialog::refresh_folder_state() {
  if (folder_status_label_ == nullptr || continue_button_ == nullptr || step_ != Step::Setup ||
      !project_.folder.isEmpty()) {
    return;
  }
  const QString folder = QDir::fromNativeSeparators(folder_edit_->text().trimmed());
  folder_book_.reset();
  QString error;
  const bool has_manifest =
      !folder.isEmpty() && QFileInfo::exists(QDir(folder).filePath(QString::fromLatin1(kBookProjectFileName)));
  if (has_manifest) {
    folder_book_ = load_book_project(folder, &error);
  }
  const bool has_book = folder_book_.has_value();
  if (has_book) {
    const QString title = folder_book_->title.trimmed().isEmpty() ? tr("Untitled") : folder_book_->title.trimmed();
    const int scans = static_cast<int>(folder_book_->scans.size()) + (folder_book_->cover ? 1 : 0);
    folder_status_label_->setText(
        folder_book_->finished_scanning
            ? tr("This folder holds \"%1\" (%n scan(s)), ready to export. Continue This Book reviews and exports it; "
                 "Start Over deletes its scans so a new book can be scanned here.",
                 nullptr, scans)
                  .arg(title)
            : tr("This folder holds \"%1\" (%n scan(s) so far). Continue This Book picks up where you stopped; "
                 "Start Over deletes its scans so a new book can be scanned here.",
                 nullptr, scans)
                  .arg(title));
    if (title_edit_->text().isEmpty() || title_from_folder_book_) {
      title_edit_->setText(folder_book_->title);
      author_edit_->setText(folder_book_->author);
      title_from_folder_book_ = true;
    }
    const int device = device_combo_->findData(folder_book_->device_id);
    if (device >= 0) {
      device_combo_->setCurrentIndex(device);
    }
  } else {
    if (title_from_folder_book_) {
      title_edit_->clear();
      author_edit_->clear();
      title_from_folder_book_ = false;
    }
    folder_status_label_->setText(has_manifest ? tr("This folder holds a book scan Patchy cannot read: %1").arg(error)
                                               : tr("A new book is saved in this folder."));
  }
  continue_button_->setVisible(has_book);
  start_over_button_->setVisible(has_book);
  scan_cover_button_->setVisible(!has_book);
  skip_cover_button_->setVisible(!has_book);
  setup_instructions_->setVisible(!has_book);
  scan_cover_button_->setDefault(!has_book);
  continue_button_->setDefault(has_book);
}

void BookScanDialog::continue_book() {
  if (scan_in_progress() || step_ != Step::Setup) {
    return;
  }
  const QString folder = QDir::fromNativeSeparators(folder_edit_->text().trimmed());
  QString error;
  auto loaded = load_book_project(folder, &error);
  if (!loaded) {
    show_scan_error(error);
    refresh_folder_state();
    return;
  }
  // The settings step needs to know what the scanner can do; the other steps open it
  // on the first scan.
  const bool to_cover = loaded->scans.empty() && !loaded->finished_scanning;
  if (to_cover && !ensure_session_open()) {
    return;
  }
  const QString device = device_combo_->currentData().toString();
  project_ = std::move(*loaded);
  // The scanner picked in the setup step (the book's own when it is listed).
  if (!device.isEmpty()) {
    project_.device_id = device;
  }
  if (!title_edit_->text().trimmed().isEmpty()) {
    project_.title = title_edit_->text().trimmed();
  }
  project_.author = author_edit_->text().trimmed();
  thumbnails_.clear();
  small_scans_.clear();
  preview_key_.clear();
  scan_views_.clear();
  scan_view_order_.clear();
  strip_signature_.clear();
  grayscale_hint_shown_ = !project_.scans.empty();
  edit_cover_ = false;
  edit_scan_.reset();
  folder_book_.reset();
  app_settings().setValue(QStringLiteral("bookScan/lastFolder"), QDir::toNativeSeparators(project_.folder));
  save_project_quietly();
  go_to(project_.finished_scanning ? Step::Review : to_cover ? Step::Cover : Step::Scanning);
}

void BookScanDialog::start_over() {
  if (scan_in_progress() || step_ != Step::Setup || !folder_book_) {
    return;
  }
  const BookProject old = *folder_book_;
  if (!quiet_) {
    const int scans = static_cast<int>(old.scans.size()) + (old.cover ? 1 : 0);
    const auto answer = show_warning_message(
        this, tr("Scan a Book to PDF"),
        tr("Delete the %n scan(s) of \"%1\" in %2 and start a new book there? Other files in the folder, such as an "
           "exported PDF, are kept. This cannot be undone.",
           nullptr, scans)
            .arg(old.title.trimmed().isEmpty() ? tr("Untitled") : old.title.trimmed(), QDir::toNativeSeparators(old.folder)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QStringLiteral("bookScanStartOverMessageBox"));
    if (answer != QMessageBox::Yes) {
      return;
    }
  }
  delete_book_project_files(old, false);
  thumbnails_.clear();
  small_scans_.clear();
  preview_key_.clear();
  scan_views_.clear();
  scan_view_order_.clear();
  strip_signature_.clear();
  refresh_folder_state();
}

void BookScanDialog::open_book_folder() {
  save_project_quietly();
  if (project_.folder.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(project_.folder))) {
    show_scan_error(tr("The book folder %1 could not be opened.").arg(QDir::toNativeSeparators(project_.folder)));
  }
}

bool BookScanDialog::ensure_session_open(bool report) {
  if (session_ == nullptr) {
    if (report) {
      show_scan_error(tr("Scanning is not available here."));
    }
    return false;
  }
  if (session_open_) {
    return true;
  }
  // Once the book is under way it keeps its scanner, even while another one is listed.
  QString device = device_combo_->currentData().toString();
  if (step_ != Step::Setup && !project_.device_id.isEmpty()) {
    device = project_.device_id;
  }
  QString error;
  if (!session_->open(device, &error)) {
    if (report) {
      show_scan_error(error.isEmpty() ? tr("The scanner could not be opened.") : error);
      if (!system_dialog_check_->isChecked()) {
        status_label_->setText(tr("Try \"Use the system scan dialog for each scan\" in step 1."));
      }
    }
    return false;
  }
  session_open_ = true;
  const std::vector<int> old_resolutions = capabilities_.resolutions;
  capabilities_ = session_->capabilities();
  project_.device_id = device;
  app_settings().setValue(QStringLiteral("bookScan/deviceId"), device);
  device_listed_ = true;
  // Opened again past the setup step (after the scanner turned off and on, say): keep the
  // capture settings to what it offers now.
  if (step_ != Step::Setup && capabilities_.resolutions != old_resolutions) {
    const int wanted = project_.capture_dpi;
    const auto& offered = capabilities_.resolutions;
    if (!offered.empty() && std::find(offered.begin(), offered.end(), wanted) == offered.end()) {
      int nearest = offered.front();
      for (const int dpi : offered) {
        if (std::abs(dpi - wanted) < std::abs(nearest - wanted)) {
          nearest = dpi;
        }
      }
      project_.capture_dpi = nearest;
      status_label_->setText(tr("The scanner does not offer %1 dpi now; scanning at %2 dpi.").arg(wanted).arg(nearest));
    }
    rescan_dpi_ = nearest_offered_dpi(rescan_dpi_);
    fill_resolution_combo();
    refresh_capture_controls();
  }
  return true;
}

void BookScanDialog::show_scan_error(const QString& text) {
  last_error_ = text;
  if (quiet_) {
    return;
  }
  (void)show_warning_message(this, tr("Scan a Book to PDF"), text, QMessageBox::Ok, QMessageBox::Ok,
                             QStringLiteral("bookScanErrorMessageBox"));
}

void BookScanDialog::save_project_quietly() {
  if (project_.folder.isEmpty()) {
    return;
  }
  try {
    save_book_project(project_);
  } catch (const std::exception& error) {
    last_error_ = QString::fromStdString(error.what());
    status_label_->setText(last_error_);
  }
}

bool BookScanDialog::scan_in_progress() const {
  return pending_ != PendingScan::None;
}

void BookScanDialog::start_cover_scan() {
  if (scan_in_progress() || !ensure_project_folder() || !ensure_session_open()) {
    return;
  }
  const ScanRequest request = cover_scan_request();
  pending_ = PendingScan::Cover;
  scan_cover_button_->setEnabled(false);
  status_label_->setText(tr("Scanning the cover..."));
  progress_bar_->setRange(0, 0);
  progress_bar_->setVisible(true);
  start_session_scan(request);
}

ScanRequest BookScanDialog::cover_scan_request() const {
  ScanRequest request;
  request.dpi = kCoverDpi;
  if (!capabilities_.resolutions.empty() &&
      std::find(capabilities_.resolutions.begin(), capabilities_.resolutions.end(), kCoverDpi) ==
          capabilities_.resolutions.end()) {
    request.dpi = capabilities_.resolutions.back();
    for (const int dpi : capabilities_.resolutions) {
      if (dpi >= kCoverDpi) {
        request.dpi = dpi;
        break;
      }
    }
  }
  request.mode = ScanColorMode::Color;
  request.output_base_path = QDir(project_.folder).filePath(scans_dir_name() + QStringLiteral("/cover-incoming"));
  return request;
}

void BookScanDialog::skip_cover() {
  // The scanner is opened here too, so the next step shows what it can do.
  if (scan_in_progress() || !ensure_project_folder() || !ensure_session_open()) {
    return;
  }
  project_.cover.reset();
  project_.include_covers = false;
  save_project_quietly();
  go_to(Step::Cover);
}

QString BookScanDialog::store_scan_file(const QString& file, const QString& base_name) {
  // Scans are kept as lossless PNG; drivers hand over BMP (WIA) or other formats.
  const QString relative = scans_dir_name() + QLatin1Char('/') + base_name + QStringLiteral(".png");
  const QString target = QDir(project_.folder).filePath(relative);
  if (QFileInfo(file).suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) == 0) {
    QFile::remove(target);
    if (QFile::rename(file, target)) {
      return relative;
    }
  }
  QImage image;
  {
    // Closed again before the driver's file is removed: Windows keeps an open file.
    QImageReader reader(file);
    reader.setAutoTransform(false);
    image = reader.read();
  }
  if (image.isNull()) {
    return {};
  }
  QFile::remove(target);
  QImageWriter writer(target, "png");
  if (!writer.write(image)) {
    return {};
  }
  QFile::remove(file);
  return relative;
}

void BookScanDialog::scan_finished(const ScanResult& result) {
  if (result.device_lost && pending_ != PendingScan::None && !scan_retried_ && retry_after_device_loss()) {
    return;
  }
  if (drop_after_scan_ || result.device_lost) {
    drop_after_scan_ = false;
    drop_session();
  }
  const PendingScan pending = pending_;
  pending_ = PendingScan::None;
  progress_bar_->setVisible(false);
  cancel_scan_button_->setVisible(false);
  scan_cover_button_->setEnabled(true);
  scan_button_->setEnabled(true);
  rescan_button_->setEnabled(!project_.scans.empty() || edit_cover_);
  finished_button_->setEnabled(true);
  const int rescan_target = rescan_target_;
  rescan_target_ = -1;
  if (result.status == ScannerAcquireStatus::Cancelled) {
    status_label_->setText(tr("Scan cancelled."));
    return;
  }
  if (result.status != ScannerAcquireStatus::Acquired) {
    const QString text = result.status == ScannerAcquireStatus::NoDevice ? tr("No scanner was found.")
                         : result.error.isEmpty()                         ? tr("The scan failed.")
                                                                          : result.error;
    status_label_->setText(text);
    show_scan_error(text);
    return;
  }
  const double dpi = result.horizontal_dpi > 0.0 ? result.horizontal_dpi : last_request_.dpi;
  if (pending == PendingScan::Cover || pending == PendingScan::RescanCover) {
    apply_cover_scan(result.file_path, result.horizontal_dpi > 0.0 ? result.horizontal_dpi : kCoverDpi,
                     pending == PendingScan::RescanCover);
  } else if (pending == PendingScan::Page) {
    apply_page_scan(result.file_path, dpi, last_request_.mode, -1);
  } else if (pending == PendingScan::Rescan) {
    apply_page_scan(result.file_path, dpi, last_request_.mode, rescan_target);
  }
  if (beep_check_->isChecked() && !quiet_) {
    QApplication::beep();
  }
}

void BookScanDialog::apply_cover_scan(const QString& file, double dpi, bool keep_settings) {
  // A new cover scan is written beside the old one first, so a failure keeps the old one.
  const bool replace = project_.cover.has_value();
  QString relative = store_scan_file(file, replace ? QStringLiteral("cover-new") : QStringLiteral("cover"));
  if (relative.isEmpty()) {
    show_scan_error(tr("The cover scan could not be saved in the project folder."));
    return;
  }
  if (replace) {
    const QString old_file = project_.cover->file;
    const QDir folder(project_.folder);
    QFile::remove(folder.filePath(old_file));
    if (QFile::rename(folder.filePath(relative), folder.filePath(old_file))) {
      relative = old_file;
    }
    forget_scan(old_file);
  }
  forget_scan(relative);
  BookCover cover;
  cover.file = relative;
  cover.dpi = dpi;
  const QImage full = read_scan_image(QDir(project_.folder).filePath(relative));
  remember_scan_copies(relative, dpi, full);
  const PixelBuffer upright = upright_pixels(full, project_.up_turns);
  BookDetectOptions options;
  options.source_ppi = dpi;
  const auto outline = detect_book_outline(upright, options);
  cover.layout = default_cover_layout(
      outline.value_or(OrientedBox{upright.width() / 2.0, upright.height() / 2.0, static_cast<double>(upright.width()),
                                   static_cast<double>(upright.height()), 0.0}));
  project_.cover = cover;
  if (keep_settings) {
    // Rescanned while scanning pages: the book's settings stay, its sizing follows the new outline.
    update_book_area();
    thumbnails_.clear();
    save_project_quietly();
    edit_cover_ = true;
    edit_scan_.reset();
    status_label_->setText(tr("The cover was scanned again."));
    refresh_scanning_page();
    return;
  }
  project_.include_covers = app_settings().value(QStringLiteral("bookScan/includeCovers"), true).toBool();
  // Two pages per scan unless the user picks otherwise: a book that fills the glass may
  // still fit, so a wide outline only brings up the notice that offers one page per scan.
  project_.mode = BookScanMode::TwoPages;
  save_project_quietly();
  status_label_->clear();
  go_to(Step::Cover);
}

std::optional<QRectF> BookScanDialog::scan_area_for_pages() const {
  if (!project_.scan_book_area || !capabilities_.scan_area || project_.mode == BookScanMode::OnePage) {
    return std::nullopt;
  }
  return project_.book_area_inches;
}

void BookScanDialog::fill_resolution_combo() {
  fill_resolution_items(resolution_combo_, project_.capture_dpi);
}

void BookScanDialog::fill_resolution_items(QComboBox* combo, int dpi) {
  const bool was_refreshing = refreshing_;
  refreshing_ = true;
  std::vector<int> resolutions = capabilities_.resolutions;
  if (resolutions.empty()) {
    resolutions = {150, 200, 300, 400, 600};
  }
  bool same = combo->count() == static_cast<int>(resolutions.size());
  for (int i = 0; same && i < combo->count(); ++i) {
    same = combo->itemData(i).toInt() == resolutions[static_cast<std::size_t>(i)];
  }
  if (!same) {
    combo->clear();
    for (const int offered : resolutions) {
      combo->addItem(tr("%1 dpi").arg(offered), offered);
    }
  }
  int index = combo->findData(dpi);
  if (index < 0) {
    index = combo->findData(nearest_offered_dpi(dpi));
  }
  if (index < 0) {
    index = combo->findData(300);
  }
  combo->setCurrentIndex(std::max(0, index));
  combo->setEnabled(capabilities_.direct_control && device_listed_);
  refreshing_ = was_refreshing;
}

int BookScanDialog::nearest_offered_dpi(int dpi) const {
  const auto& offered = capabilities_.resolutions;
  if (offered.empty() || std::find(offered.begin(), offered.end(), dpi) != offered.end()) {
    return dpi;
  }
  int nearest = offered.front();
  for (const int candidate : offered) {
    if (std::abs(candidate - dpi) < std::abs(nearest - dpi)) {
      nearest = candidate;
    }
  }
  return nearest;
}

void BookScanDialog::refresh_capture_controls() {
  if (scan_resolution_combo_ == nullptr) {
    return;
  }
  const bool was_refreshing = refreshing_;
  refreshing_ = true;
  const bool earlier = editing_earlier_scan();
  const int dpi = earlier ? rescan_dpi_ : project_.capture_dpi;
  const ScanColorMode mode = earlier ? rescan_mode_ : project_.capture_mode;
  fill_resolution_items(scan_resolution_combo_, dpi);
  scan_color_combo_->setCurrentIndex(mode == ScanColorMode::Grayscale ? 1 : 0);
  const bool controllable = capabilities_.direct_control && device_listed_;
  scan_resolution_combo_->setEnabled(controllable);
  scan_color_combo_->setEnabled(controllable);
  scan_form_->setRowVisible(scan_resolution_combo_, !edit_cover_);
  scan_form_->setRowVisible(scan_color_combo_, !edit_cover_);
  QString note;
  if (!edit_cover_ && !capabilities_.direct_control && session_open_) {
    note = tr("The scan dialog sets the resolution and mode for each scan.");
  } else if (earlier) {
    const auto& scan = project_.scans[static_cast<std::size_t>(selected_scan())];
    const int scanned_dpi = static_cast<int>(std::lround(scan.dpi));
    note = scan.capture_mode == ScanColorMode::Grayscale ? tr("Scanned at %1 dpi in grayscale.").arg(scanned_dpi)
                                                         : tr("Scanned at %1 dpi in color.").arg(scanned_dpi);
    if (scanned_dpi != rescan_dpi_ || scan.capture_mode != rescan_mode_) {
      note += QLatin1Char(' ') + tr("Rescan This Scan scans it again with the settings above.");
    }
  }
  capture_note_->setText(note);
  capture_note_->setVisible(!note.isEmpty());
  refreshing_ = was_refreshing;
}

void BookScanDialog::capture_controls_changed() {
  const int dpi = scan_resolution_combo_->currentData().toInt() > 0 ? scan_resolution_combo_->currentData().toInt() : 300;
  const ScanColorMode mode = scan_color_combo_->currentIndex() == 1 ? ScanColorMode::Grayscale : ScanColorMode::Color;
  if (editing_earlier_scan()) {
    rescan_dpi_ = dpi;
    rescan_mode_ = mode;
  } else {
    project_.capture_dpi = dpi;
    project_.capture_mode = mode;
    if (mode == ScanColorMode::Grayscale) {
      grayscale_hint_->setVisible(false);
      grayscale_switch_->setVisible(false);
    }
    save_project_quietly();
  }
  refresh_capture_controls();
}

void BookScanDialog::refresh_cover_page() {
  refreshing_ = true;
  up_combo_->setCurrentIndex((4 - project_.up_turns) % 4);
  direction_combo_->setCurrentIndex(project_.direction == BookReadingDirection::RightToLeft ? 1 : 0);
  fill_resolution_combo();
  color_combo_->setCurrentIndex(project_.capture_mode == ScanColorMode::Grayscale ? 1 : 0);
  color_combo_->setEnabled(capabilities_.direct_control && device_listed_);
  area_combo_->setCurrentIndex(project_.scan_book_area ? 0 : 1);
  area_combo_->setEnabled(capabilities_.scan_area && project_.cover.has_value());
  mode_combo_->setCurrentIndex(project_.mode == BookScanMode::OnePage ? 1 : 0);
  covers_check_->setChecked(project_.include_covers && project_.cover.has_value());
  covers_check_->setEnabled(project_.cover.has_value());
  if (project_.cover) {
    const UprightView& view = upright_preview(project_.cover->file, project_.cover->dpi, project_.up_turns);
    cover_preview_->set_image(view.image, view.full_size);
    cover_preview_->set_box(project_.cover->layout.outline);
    cover_preview_->set_lines(project_.cover->layout.spine_left, project_.cover->layout.spine_right);
    cover_tilt_spin_->setValue(project_.cover->layout.outline.angle_degrees);
    cover_tilt_spin_->setEnabled(true);
    up_combo_->setEnabled(true);
  } else {
    cover_preview_->clear_image();
    cover_tilt_spin_->setEnabled(false);
    up_combo_->setEnabled(false);
  }
  if (!capabilities_.direct_control) {
    mode_notice_->setText(tr("The scan dialog sets the resolution and mode for each scan."));
  }
  refreshing_ = false;
  update_cover_labels();
  if (project_.cover) {
    cover_settings_changed();  // the book area and the notice for the measured book
  }
  update_page_size_label();
}

void BookScanDialog::cover_settings_changed() {
  project_.capture_dpi = resolution_combo_->currentData().toInt() > 0 ? resolution_combo_->currentData().toInt() : 300;
  project_.capture_mode = color_combo_->currentIndex() == 1 ? ScanColorMode::Grayscale : ScanColorMode::Color;
  project_.scan_book_area = area_combo_->currentIndex() == 0;
  project_.mode = mode_combo_->currentIndex() == 1 ? BookScanMode::OnePage : BookScanMode::TwoPages;
  project_.include_covers = covers_check_->isChecked() && project_.cover.has_value();
  if (project_.cover) {
    update_book_area();
    const double dpi = project_.cover->dpi;
    const double width_in = project_.cover->layout.outline.width / dpi;
    const double height_in = project_.cover->layout.outline.height / dpi;
    const bool fits = book_spread_fits_bed(width_in, height_in, capabilities_.bed_width_inches,
                                           capabilities_.bed_height_inches, kBedFitMarginInches);
    // The cover scan cannot show more than the glass, so a book that reaches its edges
    // may or may not fit open: say so and leave the choice to the user.
    if (!fits && project_.mode == BookScanMode::TwoPages) {
      mode_notice_->setText(tr("The open book reaches the edges of the glass. If both pages do not fit on the "
                               "glass, choose One page per scan."));
    } else if (capabilities_.direct_control) {
      mode_notice_->clear();
    }
  }
  QSettings settings = app_settings();
  settings.setValue(QStringLiteral("bookScan/dpi"), project_.capture_dpi);
  settings.setValue(QStringLiteral("bookScan/colorMode"),
                    project_.capture_mode == ScanColorMode::Grayscale ? QStringLiteral("gray") : QStringLiteral("color"));
  settings.setValue(QStringLiteral("bookScan/scanArea"),
                    project_.scan_book_area ? QStringLiteral("book") : QStringLiteral("glass"));
  settings.setValue(QStringLiteral("bookScan/readingDirection"),
                    project_.direction == BookReadingDirection::RightToLeft ? QStringLiteral("rtl") : QStringLiteral("ltr"));
  settings.setValue(QStringLiteral("bookScan/includeCovers"), covers_check_->isChecked());
  save_project_quietly();
  update_page_size_label();
}

void BookScanDialog::update_book_area() {
  if (!project_.cover) {
    return;
  }
  // The book area in scanner coordinates: the outline's bounds in the unturned scan,
  // grown by a margin, in inches.
  const QSize raw = QImageReader(QDir(project_.folder).filePath(project_.cover->file)).size();
  const Rect upright_bounds = oriented_box_bounds(project_.cover->layout.outline);
  const Rect raw_bounds = unturned_rect(upright_bounds, project_.up_turns, raw.width(), raw.height());
  const double dpi = project_.cover->dpi;
  const QRectF area(raw_bounds.x / dpi - kBookAreaMarginInches, raw_bounds.y / dpi - kBookAreaMarginInches,
                    raw_bounds.width / dpi + 2.0 * kBookAreaMarginInches,
                    raw_bounds.height / dpi + 2.0 * kBookAreaMarginInches);
  const QRectF bed(0.0, 0.0, capabilities_.bed_width_inches > 0.0 ? capabilities_.bed_width_inches : raw.width() / dpi,
                   capabilities_.bed_height_inches > 0.0 ? capabilities_.bed_height_inches : raw.height() / dpi);
  project_.book_area_inches = area.intersected(bed);
}

void BookScanDialog::set_up_turns(int turns) {
  turns = ((turns % 4) + 4) % 4;
  if (turns == project_.up_turns || !project_.cover) {
    return;
  }
  const bool half = (turns - project_.up_turns + 4) % 4 == 2;
  project_.up_turns = turns;
  const QDir folder(project_.folder);
  const auto upright_size = [&folder](const QString& file, int quarter_turns) {
    const QSize raw = QImageReader(folder.filePath(file)).size();
    return quarter_turns % 2 != 0 ? raw.transposed() : raw;
  };
  QApplication::setOverrideCursor(Qt::WaitCursor);
  // Half a turn: the same outline, spine and pages, turned, so edits survive. A quarter
  // turn changes which way the spine runs: detect again on the turned scans.
  if (half) {
    const QSize size = upright_size(project_.cover->file, turns);
    project_.cover->layout = half_turned_cover(project_.cover->layout, size.width(), size.height());
  } else {
    const PixelBuffer upright = load_upright_scan(project_, project_.cover->file, turns);
    const auto outline = detect_book_outline(upright, {});
    project_.cover->layout = default_cover_layout(
        outline.value_or(OrientedBox{upright.width() / 2.0, upright.height() / 2.0, static_cast<double>(upright.width()),
                                     static_cast<double>(upright.height()), 0.0}));
    project_.cover->user_edited = false;
  }
  for (auto& scan : project_.scans) {
    if (half) {
      const QSize size = upright_size(scan.file, turns + scan.extra_turns);
      scan.layout = half_turned_spread(scan.layout, size.width(), size.height());
      // The left page is now on the right.
      scan.use = scan.use == BookPageUse::LeftOnly    ? BookPageUse::RightOnly
                 : scan.use == BookPageUse::RightOnly ? BookPageUse::LeftOnly
                                                      : scan.use;
    } else {
      const PixelBuffer upright = load_upright_scan(project_, scan.file, turns + scan.extra_turns);
      scan.layout = detect_scan_layout(upright, scan.dpi, std::nullopt);
      scan.user_edited = false;
    }
  }
  if (half && project_.custom_order && project_.mode == BookScanMode::TwoPages) {
    for (auto& page : *project_.custom_order) {
      if (page.kind == BookPageRef::Kind::Scan) {
        page.side = page.side == BookPageSide::Left ? BookPageSide::Right : BookPageSide::Left;
      }
    }
  }
  QApplication::restoreOverrideCursor();
  update_book_area();
  thumbnails_.clear();
  preview_key_.clear();
  save_project_quietly();
}

void BookScanDialog::update_cover_labels() {
  const bool rtl = project_.direction == BookReadingDirection::RightToLeft;
  // Seen from outside, a left-to-right book has its front cover on the right.
  cover_preview_->set_region_labels(rtl ? tr("Front cover") : tr("Back cover"), rtl ? tr("Back cover") : tr("Front cover"));
}

void BookScanDialog::update_page_size_label() {
  if (page_size_label_ == nullptr) {
    return;
  }
  if (project_.cover) {
    page_size_label_->setText(tr("about %1").arg(size_text(cover_page_size(project_.cover->layout, project_.cover->dpi))));
  } else {
    page_size_label_->setText(tr("measured from the pages"));
  }
}

void BookScanDialog::start_page_scanning() {
  if (!ensure_project_folder()) {
    return;
  }
  if (project_.cover) {
    cover_settings_changed();
  }
  edit_cover_ = false;
  edit_scan_.reset();
  save_project_quietly();
  go_to(Step::Scanning);
}

void BookScanDialog::start_page_scan() {
  if (scan_in_progress() || !ensure_project_folder() || !ensure_session_open()) {
    return;
  }
  ScanRequest request;
  request.dpi = project_.capture_dpi;
  request.mode = project_.capture_mode;
  request.area_inches = scan_area_for_pages();
  request.output_base_path =
      QDir(project_.folder).filePath(scans_dir_name() + QStringLiteral("/incoming-%1").arg(project_.scans.size() + 1));
  pending_ = PendingScan::Page;
  scan_button_->setEnabled(false);
  rescan_button_->setEnabled(false);
  finished_button_->setEnabled(false);
  cancel_scan_button_->setVisible(capabilities_.direct_control);
  status_label_->setText(tr("Scanning..."));
  progress_bar_->setRange(0, 0);
  progress_bar_->setVisible(true);
  start_session_scan(request);
}

void BookScanDialog::rescan_last() {
  if (project_.scans.empty() || scan_in_progress()) {
    return;
  }
  edit_cover_ = false;
  edit_scan_.reset();
  rescan_selected();
}

int BookScanDialog::selected_scan() const {
  if (edit_cover_ || project_.scans.empty()) {
    return -1;
  }
  const int last = static_cast<int>(project_.scans.size()) - 1;
  return edit_scan_ ? std::clamp(*edit_scan_, 0, last) : last;
}

bool BookScanDialog::editing_earlier_scan() const {
  return !edit_cover_ && edit_scan_.has_value() && selected_scan() < static_cast<int>(project_.scans.size()) - 1;
}

void BookScanDialog::edit_page(const BookPageRef& page) {
  if (page.kind == BookPageRef::Kind::FrontCover || page.kind == BookPageRef::Kind::BackCover) {
    if (!project_.cover) {
      return;
    }
    edit_cover_ = true;
    edit_scan_.reset();
  } else if (page.kind == BookPageRef::Kind::Scan && page.scan >= 0 &&
             page.scan < static_cast<int>(project_.scans.size())) {
    edit_cover_ = false;
    // The newest scan is what the step shows anyway.
    const std::optional<int> scan =
        page.scan == static_cast<int>(project_.scans.size()) - 1 ? std::nullopt : std::optional<int>(page.scan);
    if (scan && scan != edit_scan_) {
      // Rescan This Scan starts from the scan's own settings.
      const auto& entry = project_.scans[static_cast<std::size_t>(*scan)];
      rescan_dpi_ = nearest_offered_dpi(static_cast<int>(std::lround(entry.dpi)));
      rescan_mode_ = entry.capture_mode;
    }
    edit_scan_ = scan;
  } else {
    return;
  }
  if (step_ == Step::Scanning) {
    refresh_scanning_page();
  } else if (step_ == Step::Review) {
    go_to(Step::Scanning);
  }
}

void BookScanDialog::select_scan_for_editing(int index) {
  edit_page({BookPageRef::Kind::Scan, index, BookPageSide::Left, 0});
}

void BookScanDialog::select_cover_for_editing() {
  edit_page({BookPageRef::Kind::FrontCover, -1, BookPageSide::Left, 0});
}

void BookScanDialog::back_to_next_pages() {
  edit_cover_ = false;
  edit_scan_.reset();
  if (step_ == Step::Scanning) {
    refresh_scanning_page();
    scan_button_->setFocus();
  }
}

void BookScanDialog::rescan_selected() {
  if (scan_in_progress() || !ensure_session_open()) {
    return;
  }
  if (edit_cover_) {
    if (!project_.cover) {
      return;
    }
    const ScanRequest request = cover_scan_request();
    pending_ = PendingScan::RescanCover;
    scan_button_->setEnabled(false);
    rescan_button_->setEnabled(false);
    finished_button_->setEnabled(false);
    cancel_scan_button_->setVisible(capabilities_.direct_control);
    status_label_->setText(tr("Scanning the cover..."));
    progress_bar_->setRange(0, 0);
    progress_bar_->setVisible(true);
    start_session_scan(request);
    return;
  }
  const int target = selected_scan();
  if (target < 0) {
    return;
  }
  rescan_target_ = target;
  ScanRequest request;
  // An earlier scan is scanned again with its own settings (or those picked for it); the
  // newest with the next scans' settings.
  const bool earlier = editing_earlier_scan();
  request.dpi = earlier ? rescan_dpi_ : project_.capture_dpi;
  request.mode = earlier ? rescan_mode_ : project_.capture_mode;
  request.area_inches = scan_area_for_pages();
  request.output_base_path = QDir(project_.folder).filePath(scans_dir_name() + QStringLiteral("/incoming-rescan"));
  pending_ = PendingScan::Rescan;
  scan_button_->setEnabled(false);
  rescan_button_->setEnabled(false);
  finished_button_->setEnabled(false);
  cancel_scan_button_->setVisible(capabilities_.direct_control);
  status_label_->setText(tr("Scanning again..."));
  progress_bar_->setRange(0, 0);
  progress_bar_->setVisible(true);
  start_session_scan(request);
}

void BookScanDialog::apply_page_scan(const QString& file, double dpi, ScanColorMode mode, int replace_index) {
  const bool replace = replace_index >= 0 && replace_index < static_cast<int>(project_.scans.size());
  const QString old_file = replace ? project_.scans[static_cast<std::size_t>(replace_index)].file : QString();
  const QString base =
      replace ? QFileInfo(old_file).completeBaseName()
              : (project_.mode == BookScanMode::OnePage ? QStringLiteral("page-%1") : QStringLiteral("spread-%1"))
                    .arg(static_cast<int>(project_.scans.size()) + 1, 4, 10, QLatin1Char('0'));
  // A rescan writes beside the old scan first, so a failure keeps the old one.
  const QString relative = store_scan_file(file, replace ? base + QStringLiteral("-new") : base);
  if (relative.isEmpty()) {
    show_scan_error(tr("The scan could not be saved in the project folder."));
    return;
  }
  BookScanEntry entry;
  entry.file = relative;
  entry.dpi = dpi;
  // Read once: detection, the thumbnail and the box editor's view all come from it.
  const QImage full = read_scan_image(QDir(project_.folder).filePath(relative));
  // The system scan dialog picks the mode itself; a gray file says which it was.
  entry.capture_mode = capabilities_.direct_control ? mode
                       : full.isGrayscale()         ? ScanColorMode::Grayscale
                                                    : ScanColorMode::Color;
  const PixelBuffer upright = upright_pixels(full, project_.up_turns);
  std::optional<OrientedBox> fallback;
  const std::size_t previous_index = replace ? static_cast<std::size_t>(replace_index) : project_.scans.size();
  if (previous_index > 0 && previous_index <= project_.scans.size()) {
    const auto& previous = project_.scans[previous_index - 1];
    if (previous.dpi > 0.0) {
      // The same scan area at another resolution: the previous box, rescaled.
      const double ratio = dpi / previous.dpi;
      const OrientedBox& box = previous.layout.spread;
      fallback = OrientedBox{box.center_x * ratio, box.center_y * ratio, box.width * ratio, box.height * ratio,
                             box.angle_degrees};
    }
  }
  entry.layout = detect_scan_layout(upright, dpi, fallback);
  if (replace) {
    auto& target = project_.scans[static_cast<std::size_t>(replace_index)];
    // The rescanned spread keeps its place, its Use choice and any custom page order.
    entry.use = target.use;
    // Move the new scan onto the old name so the folder stays numbered.
    const QDir folder(project_.folder);
    QFile::remove(folder.filePath(old_file));
    if (QFile::rename(folder.filePath(relative), folder.filePath(old_file))) {
      entry.file = old_file;
    }
    forget_scan(old_file);
    forget_scan(entry.file);
    remember_scan_copies(entry.file, dpi, full);
    target = entry;
    edit_cover_ = false;
    edit_scan_ = replace_index == static_cast<int>(project_.scans.size()) - 1 ? std::nullopt
                                                                              : std::optional<int>(replace_index);
  } else {
    edit_cover_ = false;
    edit_scan_.reset();
    remember_scan_copies(entry.file, dpi, full);
    project_.scans.push_back(entry);
    if (project_.custom_order) {
      const int index = static_cast<int>(project_.scans.size()) - 1;
      if (project_.mode == BookScanMode::OnePage) {
        project_.custom_order->push_back({BookPageRef::Kind::Scan, index, BookPageSide::Left, 0});
      } else {
        const bool rtl = project_.direction == BookReadingDirection::RightToLeft;
        project_.custom_order->push_back({BookPageRef::Kind::Scan, index, rtl ? BookPageSide::Right : BookPageSide::Left, 0});
        project_.custom_order->push_back({BookPageRef::Kind::Scan, index, rtl ? BookPageSide::Left : BookPageSide::Right, 0});
      }
    }
  }
  thumbnails_.clear();
  save_project_quietly();
  // After the first spread of a color capture, offer grayscale when the pages hold no color.
  if (!grayscale_hint_shown_ && project_.scans.size() == 1 && project_.capture_mode == ScanColorMode::Color) {
    grayscale_hint_shown_ = true;
    BookScanCache cache(project_);
    const BookPageSize size = resolved_page_size(project_);
    bool gray = true;
    for (const auto& page : generated_page_order(project_)) {
      if (page.kind == BookPageRef::Kind::Scan) {
        gray = gray && book_page_looks_grayscale(render_book_page(project_, page, size, 50, cache, false));
      }
    }
    grayscale_hint_->setVisible(gray);
    grayscale_switch_->setVisible(gray);
  }
  status_label_->setText(replace ? tr("Scan %1 was scanned again.").arg(replace_index + 1)
                                 : tr("Scan %1 done.").arg(project_.scans.size()));
  refresh_scanning_page();
}

SpreadLayout BookScanDialog::detect_scan_layout(const PixelBuffer& upright, double dpi,
                                                const std::optional<OrientedBox>& fallback) const {
  BookDetectOptions options;
  options.source_ppi = dpi;
  if (project_.mode != BookScanMode::OnePage) {
    return detect_spread(upright, options, fallback);
  }
  SpreadLayout layout;
  layout.spread = detect_book_outline(upright, options)
                      .value_or(fallback.value_or(OrientedBox{upright.width() / 2.0, upright.height() / 2.0,
                                                              static_cast<double>(upright.width()),
                                                              static_cast<double>(upright.height()), 0.0}));
  return layout;
}

void BookScanDialog::set_scan_mode(BookScanMode mode) {
  if (mode == project_.mode || scan_in_progress()) {
    refresh_scanning_page();
    return;
  }
  project_.mode = mode;
  if (!project_.scans.empty()) {
    // The scans so far hold the other number of pages: find their pages again. Page
    // edits and a custom page order belong to the old pages, so they go.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (auto& scan : project_.scans) {
      const PixelBuffer upright = load_upright_scan(project_, scan.file, project_.up_turns + scan.extra_turns);
      scan.layout = detect_scan_layout(upright, scan.dpi, std::nullopt);
      scan.use = BookPageUse::Both;
      scan.user_edited = false;
    }
    project_.custom_order.reset();
    QApplication::restoreOverrideCursor();
  }
  thumbnails_.clear();
  save_project_quietly();
  status_label_->setText(mode == BookScanMode::OnePage ? tr("Each scan now holds one page.")
                                                       : tr("Each scan now holds two pages."));
  refresh_scanning_page();
}

PixelBuffer BookScanDialog::cut_page_from_copy(const BookPageRef& page, const BookPageSize& size, double dpi,
                                               bool view_copy) {
  const auto out_width = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(size.width * dpi)));
  const auto out_height = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::lround(size.height * dpi)));
  PixelBuffer cut;
  if (page.kind == BookPageRef::Kind::Blank) {
    cut = PixelBuffer(out_width, out_height, PixelFormat::gray8());
    cut.clear(255);
    return cut;
  }
  const auto source = book_page_source(project_, page, size);
  if (!source) {
    return {};
  }
  const PixelBuffer* pixels = nullptr;
  double scale = 1.0;
  if (view_copy) {
    if (const ScanView* view = scan_view(source->file, source->dpi)) {
      pixels = &view->pixels;
      scale = view->scale;
    }
  } else if (const SmallScan* small = small_scan(source->file, source->dpi)) {
    pixels = &small->pixels;
    scale = small->scale;
  }
  if (pixels == nullptr || pixels->empty()) {
    return {};
  }
  const PixelBuffer upright = source->turns == 0 ? *pixels : rotated_quarter_turns(*pixels, source->turns);
  OrientedBox box = source->box;
  box.center_x *= scale;
  box.center_y *= scale;
  box.width *= scale;
  box.height *= scale;
  cut = extract_book_page(upright, box, out_width, out_height);
  if (!cut.empty() && page.turns % 4 != 0) {
    cut = rotated_quarter_turns(cut, page.turns);
  }
  return cut;
}

QImage BookScanDialog::thumbnail_for(const BookPageRef& page) {
  const BookPageSize size = resolved_page_size(project_);
  const QString key = thumbnail_key(page, size);
  const auto found = thumbnails_.constFind(key);
  if (found != thumbnails_.constEnd()) {
    return found.value();
  }
  const QImage image = book_image_from_pixels(cut_page_from_copy(page, size, kThumbnailDpi, false));
  thumbnails_.insert(key, image);
  return image;
}

void BookScanDialog::remember_scan_copies(const QString& file, double dpi, const QImage& full) {
  if (full.isNull() || dpi <= 0.0 || project_.folder.isEmpty()) {
    return;
  }
  const QDir folder(project_.folder);
  QDir().mkpath(folder.filePath(QStringLiteral("thumbs")));
  // The view copy first; the small copy is made from it, which is far cheaper than from
  // the full scan and just as sharp at its size.
  const int longest = std::max(full.width(), full.height());
  const double view_factor = longest > kViewLongestSide ? static_cast<double>(kViewLongestSide) / longest : 1.0;
  const QImage view = view_factor < 1.0
                          ? full.scaled(std::max(1, static_cast<int>(std::lround(full.width() * view_factor))),
                                        std::max(1, static_cast<int>(std::lround(full.height() * view_factor))),
                                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                          : full;
  QImageWriter view_writer(folder.filePath(view_copy_file(file)), "jpg");
  view_writer.setQuality(kViewJpegQuality);
  (void)view_writer.write(view);
  const double small_factor = std::min(1.0, kSmallScanDpi / dpi);
  const QImage small = view.scaled(std::max(1, static_cast<int>(std::lround(full.width() * small_factor))),
                                   std::max(1, static_cast<int>(std::lround(full.height() * small_factor))),
                                   Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  (void)QImageWriter(folder.filePath(QStringLiteral("thumbs/") + QFileInfo(file).fileName()), "png").write(small);

  SmallScan small_entry;
  small_entry.pixels = book_pixels_from_image(small);
  small_entry.scale = static_cast<double>(small.width()) / full.width();
  small_scans_.insert_or_assign(file, std::move(small_entry));
  ScanView view_entry;
  view_entry.pixels = book_pixels_from_image(view);
  view_entry.full_size = full.size();
  view_entry.scale = static_cast<double>(view.width()) / full.width();
  scan_views_.insert_or_assign(file, std::move(view_entry));
  std::erase(scan_view_order_, file);
  scan_view_order_.push_back(file);
  while (scan_view_order_.size() > kViewCacheCapacity) {
    scan_views_.erase(scan_view_order_.front());
    scan_view_order_.erase(scan_view_order_.begin());
  }
}

bool BookScanDialog::decode_scan_copies(const QString& file, double dpi) {
  const QImage full = read_scan_image(QDir(project_.folder).filePath(file));
  if (full.isNull()) {
    return false;
  }
  remember_scan_copies(file, dpi, full);
  return true;
}

const BookScanDialog::SmallScan* BookScanDialog::small_scan(const QString& file, double dpi) {
  const auto found = small_scans_.find(file);
  if (found != small_scans_.end()) {
    return &found->second;
  }
  const QDir folder(project_.folder);
  const QString full_path = folder.filePath(file);
  const QSize full_size = QImageReader(full_path).size();
  if (!full_size.isValid() || dpi <= 0.0) {
    return nullptr;
  }
  const QString thumb_path = folder.filePath(QStringLiteral("thumbs/") + QFileInfo(file).fileName());
  QImage small;
  if (QFileInfo(thumb_path).lastModified() >= QFileInfo(full_path).lastModified()) {
    small = QImageReader(thumb_path).read();
  }
  if (small.isNull()) {
    // Missing or older than its scan: make both copies (the view copy is needed next).
    if (!decode_scan_copies(file, dpi)) {
      return nullptr;
    }
    const auto made = small_scans_.find(file);
    return made != small_scans_.end() ? &made->second : nullptr;
  }
  SmallScan entry;
  entry.pixels = book_pixels_from_image(small);
  entry.scale = static_cast<double>(small.width()) / full_size.width();
  return &small_scans_.emplace(file, std::move(entry)).first->second;
}

const BookScanDialog::ScanView* BookScanDialog::scan_view(const QString& file, double dpi) {
  const auto found = scan_views_.find(file);
  if (found != scan_views_.end()) {
    std::erase(scan_view_order_, file);
    scan_view_order_.push_back(file);
    return &found->second;
  }
  const QDir folder(project_.folder);
  const QString full_path = folder.filePath(file);
  const QSize full_size = QImageReader(full_path).size();
  if (!full_size.isValid() || dpi <= 0.0) {
    return nullptr;
  }
  const QString view_path = folder.filePath(view_copy_file(file));
  QImage view;
  if (QFileInfo(view_path).lastModified() >= QFileInfo(full_path).lastModified()) {
    view = QImageReader(view_path).read();
  }
  if (view.isNull()) {
    if (!decode_scan_copies(file, dpi)) {
      return nullptr;
    }
    const auto made = scan_views_.find(file);
    return made != scan_views_.end() ? &made->second : nullptr;
  }
  ScanView entry;
  entry.pixels = book_pixels_from_image(view);
  entry.full_size = full_size;
  entry.scale = static_cast<double>(view.width()) / full_size.width();
  scan_view_order_.push_back(file);
  while (scan_view_order_.size() > kViewCacheCapacity) {
    scan_views_.erase(scan_view_order_.front());
    scan_view_order_.erase(scan_view_order_.begin());
  }
  return &scan_views_.insert_or_assign(file, std::move(entry)).first->second;
}

void BookScanDialog::forget_scan(const QString& file) {
  small_scans_.erase(file);
  scan_views_.erase(file);
  std::erase(scan_view_order_, file);
  const QDir folder(project_.folder);
  QFile::remove(folder.filePath(QStringLiteral("thumbs/") + QFileInfo(file).fileName()));
  QFile::remove(folder.filePath(view_copy_file(file)));
  thumbnails_.clear();
  preview_key_.clear();
}

const BookScanDialog::UprightView& BookScanDialog::upright_preview(const QString& file, double dpi, int turns) {
  const int normalized = ((turns % 4) + 4) % 4;
  const QString key = file + QLatin1Char('/') + QString::number(normalized);
  if (key != preview_key_) {
    preview_ = {};
    if (const ScanView* view = scan_view(file, dpi)) {
      preview_.image = book_image_from_pixels(normalized == 0 ? view->pixels : rotated_quarter_turns(view->pixels, normalized));
      preview_.full_size = normalized % 2 != 0 ? view->full_size.transposed() : view->full_size;
    }
    preview_key_ = key;
  }
  return preview_;
}

void BookScanDialog::refresh_scanning_page() {
  refreshing_ = true;
  const bool one_page = project_.mode == BookScanMode::OnePage;
  const int scanned_pages = [this] {
    int count = 0;
    for (const auto& scan : project_.scans) {
      count += pages_per_scan(project_, scan);
    }
    return count;
  }();
  if (edit_cover_ && !project_.cover) {
    edit_cover_ = false;
  }
  const int selected = selected_scan();
  const bool earlier = editing_earlier_scan();
  if (edit_cover_ || earlier) {
    const QString editing =
        edit_cover_ ? tr("Editing the cover: drag its outline and the two spine lines onto the spine's edges, or press "
                         "Rescan Cover.")
                    : tr("Editing scan %1 of %2: adjust its boxes and options, or press Rescan This Scan.")
                          .arg(selected + 1)
                          .arg(static_cast<int>(project_.scans.size()));
    const QString next = one_page ? tr("Press Back to Next Pages to go on with page %1.").arg(scanned_pages + 1)
                                  : tr("Press Back to Next Pages to go on with pages %1 and %2.")
                                        .arg(scanned_pages + 1)
                                        .arg(scanned_pages + 2);
    instruction_label_->setText(editing + QStringLiteral("\n\n") + next);
  } else if (project_.scans.empty()) {
    instruction_label_->setText(
        one_page ? tr("One page per scan: lay the first page face down on the glass, then press Scan (or Space).")
                 : tr("Open the book at the first pages, lay it face down on the glass, then press Scan (or Space)."));
  } else if (one_page) {
    instruction_label_->setText(
        tr("One page per scan: lay page %1 face down on the glass, then press Scan. Press Finished after the last page.")
            .arg(scanned_pages + 1));
  } else {
    instruction_label_->setText(tr("Turn to pages %1 and %2, lay the book face down, then press Scan. Press Finished "
                                   "after the last pages.")
                                    .arg(scanned_pages + 1)
                                    .arg(scanned_pages + 2));
  }
  spread_preview_->set_kind(edit_cover_ ? BookScanPreview::Kind::Cover
                            : one_page  ? BookScanPreview::Kind::Page
                                        : BookScanPreview::Kind::Spread);
  rescan_button_->setText(edit_cover_ ? tr("Rescan Cover") : earlier ? tr("Rescan This Scan") : tr("Rescan Last"));
  rescan_button_->setEnabled((edit_cover_ || !project_.scans.empty()) && !scan_in_progress());
  back_to_next_button_->setVisible(edit_cover_ || earlier);
  pages_combo_->setCurrentIndex(one_page ? 1 : 0);
  pages_combo_->setEnabled(!scan_in_progress());
  scan_form_->setRowVisible(use_combo_, !one_page && !edit_cover_);
  scan_form_->setRowVisible(scan_up_combo_, edit_cover_);
  scan_up_combo_->setCurrentIndex((4 - project_.up_turns) % 4);
  rotate_scan_button_->setVisible(!edit_cover_);
  use_combo_->setEnabled(selected >= 0);
  spread_tilt_spin_->setEnabled(edit_cover_ || selected >= 0);
  if (edit_cover_) {
    const bool rtl = project_.direction == BookReadingDirection::RightToLeft;
    const UprightView& view = upright_preview(project_.cover->file, project_.cover->dpi, project_.up_turns);
    spread_preview_->set_image(view.image, view.full_size);
    spread_preview_->set_box(project_.cover->layout.outline);
    spread_preview_->set_lines(project_.cover->layout.spine_left, project_.cover->layout.spine_right);
    spread_preview_->set_region_labels(rtl ? tr("Front cover") : tr("Back cover"),
                                       rtl ? tr("Back cover") : tr("Front cover"));
    spread_tilt_spin_->setValue(project_.cover->layout.outline.angle_degrees);
    edge_warning_->setVisible(false);
  } else if (selected < 0) {
    spread_preview_->clear_image();
    edge_warning_->setVisible(false);
  } else {
    const auto& scan = project_.scans[static_cast<std::size_t>(selected)];
    const UprightView& view = upright_preview(scan.file, scan.dpi, project_.up_turns + scan.extra_turns);
    const QSize upright = view.full_size;
    spread_preview_->set_image(view.image, upright);
    spread_preview_->set_box(scan.layout.spread);
    spread_preview_->set_lines(scan.layout.gutter, scan.layout.gutter);
    use_combo_->setCurrentIndex(scan.use == BookPageUse::LeftOnly ? 1 : scan.use == BookPageUse::RightOnly ? 2 : 0);
    spread_tilt_spin_->setValue(scan.layout.spread.angle_degrees);
    edge_warning_->setVisible(
        oriented_box_touches_edge(scan.layout.spread, upright.width(), upright.height(), kEdgeWarningPixels));
  }
  // Every page so far, the covers included; a click selects its scan for editing.
  std::vector<BookPageRef> strip_pages;
  const auto order = book_page_order(project_);
  const bool covers_listed = std::any_of(order.begin(), order.end(), [](const BookPageRef& page) {
    return page.kind == BookPageRef::Kind::FrontCover || page.kind == BookPageRef::Kind::BackCover;
  });
  if (project_.cover && !covers_listed) {
    // A cover left out of the PDF still sets the book area and the cover page size.
    strip_pages.push_back({BookPageRef::Kind::FrontCover, -1, BookPageSide::Left, 0});
  }
  for (const auto& page : order) {
    if (page.kind != BookPageRef::Kind::Blank) {
      strip_pages.push_back(page);
    }
  }
  int page_number = 0;
  QStringList labels;
  std::vector<QImage> thumbnails;
  QStringList signature;
  for (const auto& page : strip_pages) {
    const bool cover_page = page.kind == BookPageRef::Kind::FrontCover || page.kind == BookPageRef::Kind::BackCover;
    QString label;
    if (!covers_listed && cover_page) {
      label = tr("Cover");
    } else if (page.kind == BookPageRef::Kind::FrontCover) {
      label = tr("Front cover");
    } else if (page.kind == BookPageRef::Kind::BackCover) {
      label = tr("Back cover");
    } else {
      label = QString::number(++page_number);
    }
    thumbnails.push_back(thumbnail_for(page));
    signature << label + QLatin1Char('|') + thumbnail_key(page, {}) + QLatin1Char('|') +
                     QString::number(page.output ? static_cast<int>(*page.output) : -1) + QLatin1Char('|') +
                     QString::number(thumbnails.back().cacheKey());
    labels << label;
  }
  // A click that only selects another scan keeps the items and moves the selection.
  if (signature != strip_signature_ || strip_->count() != static_cast<int>(strip_pages.size())) {
    strip_->clear();
    for (std::size_t i = 0; i < strip_pages.size(); ++i) {
      auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnails[i])), labels[static_cast<qsizetype>(i)], strip_);
      item->setData(Qt::UserRole, page_variant(strip_pages[i]));
    }
    strip_signature_ = signature;
  }
  QListWidgetItem* first_selected = nullptr;
  for (std::size_t i = 0; i < strip_pages.size(); ++i) {
    const auto& page = strip_pages[i];
    const bool cover_page = page.kind == BookPageRef::Kind::FrontCover || page.kind == BookPageRef::Kind::BackCover;
    const bool selected_page = cover_page ? edit_cover_ : (!edit_cover_ && page.scan == selected);
    QListWidgetItem* item = strip_->item(static_cast<int>(i));
    if (item->isSelected() != selected_page) {
      item->setSelected(selected_page);
    }
    if (selected_page && first_selected == nullptr) {
      first_selected = item;
    }
  }
  if ((edit_cover_ || earlier) && first_selected != nullptr) {
    strip_->scrollToItem(first_selected);
  } else {
    strip_->scrollToBottom();
  }
  finished_button_->setText(tr("Finished (%n page(s))", nullptr, scanned_pages));
  refresh_capture_controls();
  refreshing_ = false;
}

void BookScanDialog::finish_scanning() {
  if (scan_in_progress()) {
    return;
  }
  project_.finished_scanning = true;
  save_project_quietly();
  go_to(Step::Review);
}

void BookScanDialog::refresh_review_page() {
  refreshing_ = true;
  const int current = pages_list_->currentRow();
  pages_list_->clear();
  const auto pages = book_page_order(project_);
  for (std::size_t i = 0; i < pages.size(); ++i) {
    const auto& page = pages[i];
    QString label;
    switch (page.kind) {
      case BookPageRef::Kind::FrontCover:
        label = tr("Front cover");
        break;
      case BookPageRef::Kind::BackCover:
        label = tr("Back cover");
        break;
      case BookPageRef::Kind::Blank:
        label = tr("Blank");
        break;
      case BookPageRef::Kind::Scan:
        label = QString::number(static_cast<int>(i) + 1);
        break;
    }
    // A page with its own output says so under its thumbnail.
    if (page.output) {
      const QString mode = *page.output == BookOutputMode::Grayscale       ? tr("Gray", "page output")
                           : *page.output == BookOutputMode::BlackAndWhite ? tr("B/W", "page output")
                                                                           : tr("Color", "page output");
      label = tr("%1 (%2)", "page label and its own output").arg(label, mode);
    }
    auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnail_for(page))), label, pages_list_);
    item->setData(Qt::UserRole, page_variant(page));
  }
  if (current >= 0 && current < pages_list_->count()) {
    pages_list_->setCurrentRow(current);
  } else if (pages_list_->count() > 0) {
    pages_list_->setCurrentRow(0);
  }
  bool any_bw = false;
  bool any_encoded = false;
  for (const auto& page : pages) {
    const bool bw = page_output_options(project_, page).mode == BookOutputMode::BlackAndWhite;
    any_bw = any_bw || bw;
    any_encoded = any_encoded || !bw;
  }
  QString size_token = QStringLiteral("median");
  switch (project_.size_policy) {
    case BookPageSizePolicy::Largest:
      size_token = QStringLiteral("largest");
      break;
    case BookPageSizePolicy::Cover:
      size_token = QStringLiteral("cover");
      break;
    case BookPageSizePolicy::Custom:
      size_token = QStringLiteral("custom");
      for (const auto& preset : kSizePresets) {
        if (std::abs(project_.custom_size.width - preset.width) < 1e-6 &&
            std::abs(project_.custom_size.height - preset.height) < 1e-6) {
          size_token = QString::fromLatin1(preset.token);
        }
      }
      break;
    case BookPageSizePolicy::Median:
      break;
  }
  size_combo_->setCurrentIndex(std::max(0, size_combo_->findData(size_token)));
  const int cover_index = size_combo_->findData(QStringLiteral("cover"));
  if (auto* model = qobject_cast<QStandardItemModel*>(size_combo_->model()); model != nullptr && cover_index >= 0) {
    model->item(cover_index)->setEnabled(project_.cover.has_value());
  }
  const BookPageSize size = resolved_page_size(project_);
  const double factor = metric_units() ? 25.4 : 1.0;
  custom_width_->setValue(size.width * factor);
  custom_height_->setValue(size.height * factor);
  custom_width_->setEnabled(project_.size_policy == BookPageSizePolicy::Custom);
  custom_height_->setEnabled(project_.size_policy == BookPageSizePolicy::Custom);
  output_combo_->setCurrentIndex(project_.output.mode == BookOutputMode::Grayscale       ? 1
                                 : project_.output.mode == BookOutputMode::BlackAndWhite ? 2
                                                                                         : 0);
  threshold_slider_->setValue(project_.output.threshold);
  threshold_slider_->setEnabled(any_bw);
  output_dpi_combo_->setCurrentIndex(std::max(0, output_dpi_combo_->findData(project_.output_dpi)));
  quality_combo_->setCurrentIndex(std::max(0, quality_combo_->findData(project_.quality_id)));
  quality_combo_->setEnabled(any_encoded);
  review_summary_->setText(tr("%n page(s), each %1.", nullptr, static_cast<int>(pages.size())).arg(size_text(size)));
  refreshing_ = false;
  update_page_output_combo();
  refresh_review_preview();
}

void BookScanDialog::refresh_review_preview() {
  if (refreshing_ || review_preview_ == nullptr) {
    return;
  }
  QListWidgetItem* item = pages_list_->currentItem();
  if (item == nullptr) {
    review_preview_->clear();
    return;
  }
  // Cut from the scan's view copy: a click on a page never reads the full scan.
  const BookPageRef page = page_from_variant(item->data(Qt::UserRole));
  const PixelBuffer cut = cut_page_from_copy(page, resolved_page_size(project_), kReviewPreviewDpi, true);
  const QImage image = cut.empty() ? QImage() : book_image_from_pixels(apply_book_output(cut, page_output_options(project_, page)));
  review_preview_->setPixmap(QPixmap::fromImage(image).scaled(review_preview_->size(), Qt::KeepAspectRatio,
                                                              Qt::SmoothTransformation));
}

std::vector<BookPageRef> BookScanDialog::review_order() const {
  std::vector<BookPageRef> order;
  order.reserve(static_cast<std::size_t>(pages_list_->count()));
  for (int row = 0; row < pages_list_->count(); ++row) {
    order.push_back(page_from_variant(pages_list_->item(row)->data(Qt::UserRole)));
  }
  return order;
}

std::vector<int> BookScanDialog::selected_review_rows() const {
  std::vector<int> rows;
  for (int row = 0; row < pages_list_->count(); ++row) {
    if (pages_list_->item(row)->isSelected()) {
      rows.push_back(row);
    }
  }
  return rows;
}

void BookScanDialog::select_review_rows(const std::vector<int>& rows) {
  if (rows.empty()) {
    return;
  }
  const bool was_refreshing = refreshing_;
  refreshing_ = true;
  pages_list_->clearSelection();
  for (const int row : rows) {
    if (QListWidgetItem* item = pages_list_->item(row)) {
      item->setSelected(true);
    }
  }
  refreshing_ = was_refreshing;
  pages_list_->setCurrentRow(rows.front(), QItemSelectionModel::NoUpdate);
  update_page_output_combo();
}

void BookScanDialog::move_review_pages(const std::vector<int>& rows, int before) {
  if (step_ != Step::Review) {
    return;
  }
  const std::vector<BookPageRef> order = review_order();
  const int count = static_cast<int>(order.size());
  std::vector<int> moving;
  for (const int row : rows) {
    if (row >= 0 && row < count) {
      moving.push_back(row);
    }
  }
  std::sort(moving.begin(), moving.end());
  moving.erase(std::unique(moving.begin(), moving.end()), moving.end());
  if (moving.empty()) {
    return;
  }
  before = std::clamp(before, 0, count);
  std::vector<BookPageRef> moved;
  std::vector<BookPageRef> rest;
  int insert_at = before;
  for (int row = 0; row < count; ++row) {
    if (std::binary_search(moving.begin(), moving.end(), row)) {
      moved.push_back(order[static_cast<std::size_t>(row)]);
      if (row < before) {
        --insert_at;
      }
    } else {
      rest.push_back(order[static_cast<std::size_t>(row)]);
    }
  }
  std::vector<BookPageRef> reordered = rest;
  reordered.insert(reordered.begin() + insert_at, moved.begin(), moved.end());
  std::vector<int> landed;
  for (int i = 0; i < static_cast<int>(moved.size()); ++i) {
    landed.push_back(insert_at + i);
  }
  if (reordered == order) {
    select_review_rows(landed);
    return;
  }
  project_.custom_order = std::move(reordered);
  save_project_quietly();
  refresh_review_page();
  select_review_rows(landed);
  refresh_review_preview();
}

void BookScanDialog::update_page_output_combo() {
  if (page_output_combo_ == nullptr || refreshing_) {
    return;
  }
  QListWidgetItem* lead = pages_list_->currentItem();
  if (lead == nullptr || !lead->isSelected()) {
    const auto selected = pages_list_->selectedItems();
    lead = selected.isEmpty() ? nullptr : selected.front();
  }
  const bool was_refreshing = refreshing_;
  refreshing_ = true;
  page_output_combo_->setEnabled(lead != nullptr);
  const BookPageRef page = lead != nullptr ? page_from_variant(lead->data(Qt::UserRole)) : BookPageRef{};
  page_output_combo_->setCurrentIndex(
      std::max(0, page_output_combo_->findData(page.output ? static_cast<int>(*page.output) : -1)));
  refreshing_ = was_refreshing;
}

void BookScanDialog::page_output_changed() {
  const std::vector<int> selected = selected_review_rows();
  if (selected.empty()) {
    return;
  }
  const int value = page_output_combo_->currentData().toInt();
  const std::optional<BookOutputMode> output =
      value < 0 ? std::nullopt : std::optional<BookOutputMode>(static_cast<BookOutputMode>(value));
  std::vector<BookPageRef> order = review_order();
  bool changed = false;
  for (const int row : selected) {
    auto& page = order[static_cast<std::size_t>(row)];
    if (page.output != output) {
      page.output = output;
      changed = true;
    }
  }
  if (!changed) {
    return;
  }
  const int current = pages_list_->currentRow();
  project_.custom_order = std::move(order);
  save_project_quietly();
  refresh_review_page();
  select_review_rows(selected);
  if (current >= 0) {
    pages_list_->setCurrentRow(current, QItemSelectionModel::NoUpdate);
  }
  refresh_review_preview();
}

void BookScanDialog::delete_selected_pages() {
  std::vector<BookPageRef> order;
  for (int row = 0; row < pages_list_->count(); ++row) {
    if (!pages_list_->item(row)->isSelected()) {
      order.push_back(page_from_variant(pages_list_->item(row)->data(Qt::UserRole)));
    }
  }
  if (order.size() == static_cast<std::size_t>(pages_list_->count())) {
    return;
  }
  project_.custom_order = std::move(order);
  save_project_quietly();
  refresh_review_page();
}

void BookScanDialog::rotate_selected_pages() {
  std::vector<BookPageRef> order;
  bool changed = false;
  for (int row = 0; row < pages_list_->count(); ++row) {
    BookPageRef page = page_from_variant(pages_list_->item(row)->data(Qt::UserRole));
    if (pages_list_->item(row)->isSelected()) {
      page.turns = (page.turns + 2) % 4;
      changed = true;
    }
    order.push_back(page);
  }
  if (!changed) {
    return;
  }
  const std::vector<int> selected = selected_review_rows();
  project_.custom_order = std::move(order);
  save_project_quietly();
  refresh_review_page();
  select_review_rows(selected);
}

void BookScanDialog::insert_blank_page() {
  std::vector<BookPageRef> order;
  const int at = pages_list_->currentRow() < 0 ? pages_list_->count() : pages_list_->currentRow() + 1;
  for (int row = 0; row < pages_list_->count(); ++row) {
    order.push_back(page_from_variant(pages_list_->item(row)->data(Qt::UserRole)));
  }
  order.insert(order.begin() + at, BookPageRef{BookPageRef::Kind::Blank, -1, BookPageSide::Left, 0});
  project_.custom_order = std::move(order);
  save_project_quietly();
  refresh_review_page();
  pages_list_->setCurrentRow(at);
}

bool BookScanDialog::export_pdf(const QString& path, bool ask_keep_folder) {
  if (path.isEmpty()) {
    return false;
  }
  save_project_quietly();
  const int count = static_cast<int>(book_page_order(project_).size());
  QProgressDialog progress(tr("Writing the book PDF..."), tr("Cancel"), 0, count, this);
  progress.setObjectName(QStringLiteral("bookScanExportProgress"));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(400);
  const BookPdfResult result = write_book_pdf(project_, path, [&progress](int done, int total) {
    progress.setLabelText(tr("Writing page %1 of %2...").arg(done).arg(total));
    progress.setValue(done);
    QCoreApplication::processEvents();
    return !progress.wasCanceled();
  });
  progress.reset();
  if (result.cancelled) {
    review_summary_->setText(tr("Export cancelled."));
    return false;
  }
  if (!result.ok) {
    show_scan_error(result.error.isEmpty() ? tr("The PDF could not be written.") : result.error);
    return false;
  }
  last_error_.clear();
  review_summary_->setText(tr("Wrote %n page(s) to %1", nullptr, result.pages).arg(QDir::toNativeSeparators(path)));
  if (!ask_keep_folder || quiet_) {
    return true;
  }
  const auto answer = show_warning_message(
      this, tr("Scan a Book to PDF"),
      tr("The PDF is written. Keep the scan project folder (%1) so you can change pages and export again later?")
          .arg(QDir::toNativeSeparators(project_.folder)),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes, QStringLiteral("bookScanKeepFolderMessageBox"));
  if (answer == QMessageBox::No) {
    delete_book_project_files(project_, true);
    project_.folder.clear();
    accept();
  }
  return true;
}


void BookScanDialog::done(int result) {
  if (scan_in_progress() && session_ != nullptr) {
    session_->cancel();
  }
  if (step_ != Step::Setup) {
    save_project_quietly();
  }
  QDialog::done(result);
}

void run_book_scan_dialog(QWidget* parent) {
  BookScanDialog dialog(parent);
  run_non_modal_dialog(dialog);
}

}  // namespace patchy::ui
