#include "ui/contour_editor_dialog.hpp"

#include "core/contour_editing.hpp"
#include "core/contour_presets.hpp"
#include "core/style_contour.hpp"
#include "ui/contour_library.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/preset_grid_widget.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFocusEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIconEngine>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOption>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace patchy::ui {

namespace {

constexpr int kGraphMargin = 10;
// How far outside the graph a dragged point must be pulled before it is removed.
constexpr int kRemoveDistance = 24;
constexpr double kHitRadius = 10.0;
// The name prompt's width, unless its title needs more.
constexpr int kContourNameDialogWidth = 340;

QString graph_tool_tip() {
  return QObject::tr("Click to add a point. Drag a point to move it, or off the graph to remove it.");
}

// Thumbnail of a contour curve: the engine LUT drawn as a polyline on a chip. The
// chip is painted from theme() roles when the icon is drawn, never baked into a
// pixmap at build time, so the combo follows a scheme change without rebuilding.
class ContourPresetIconEngine final : public QIconEngine {
public:
  explicit ContourPresetIconEngine(StyleContour contour) : contour_(std::move(contour)) {}

  void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
    painter->drawPixmap(rect, pixmap(rect.size(), mode, state));
  }

  // The chip keeps its 4:3 shape at any size: 32 x 24 in the pickers, larger in
  // the pickers' grid (ContourPickerCombo), drawn afresh at the size asked for.
  QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override {
    if (!size.isValid() || size.isEmpty()) {
      return QSize(kWidth, kHeight);
    }
    return QSize(kWidth, kHeight).scaled(size, Qt::KeepAspectRatio).expandedTo(QSize(8, 6));
  }

  QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
    auto chip = QPixmap::fromImage(chip_image(actualSize(size, mode, state)));
    if (mode == QIcon::Disabled) {
      QStyleOption option;
      return QApplication::style()->generatedIconPixmap(QIcon::Disabled, chip, &option);
    }
    return chip;
  }

  QIconEngine* clone() const override { return new ContourPresetIconEngine(*this); }

private:
  static constexpr int kWidth = 32;
  static constexpr int kHeight = 24;

  [[nodiscard]] QImage chip_image(QSize size) const {
    const auto& palette = theme();
    const int width = size.width();
    const int height = size.height();
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(palette.field_bg_large);
    QPainter painter(&image);
    painter.setPen(palette.field_border);
    painter.drawRect(0, 0, width - 1, height - 1);
    const auto lut = build_style_contour_lut(contour_);
    QPolygonF line;
    for (int x = 0; x < width; ++x) {
      const auto t = static_cast<float>(x) / static_cast<float>(width - 1);
      const auto value = sample_style_contour_lut(lut, t, true);
      line << QPointF(1.0 + x * (width - 3.0) / (width - 1.0), (height - 2.0) - value * (height - 4.0) + 1.0);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(palette.icon_ink, std::max(1.2, width / 26.0)));
    painter.drawPolyline(line);
    painter.end();
    return image;
  }

  StyleContour contour_;
};

}  // namespace

QIcon contour_preset_icon(const StyleContour& contour) { return QIcon(new ContourPresetIconEngine(contour)); }

ContourGraphWidget::ContourGraphWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("contourEditorGraph"));
  setMinimumSize(256 + 2 * kGraphMargin, 256 + 2 * kGraphMargin);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName(QObject::tr("Contour graph"));
  setToolTip(graph_tool_tip());
  // A drag move re-renders the layer's effects through the caller's preview.
  coalesce_widget_drag_moves(*this);
  set_contour(StyleContour{});
}

void ContourGraphWidget::set_contour(const StyleContour& contour) {
  contour_ = editable_style_contour(contour);
  lut_ = build_style_contour_lut(contour_);
  selected_ = std::clamp(selected_, 0, static_cast<int>(contour_.points.size()) - 1);
  drag_index_ = -1;
  update();
}

void ContourGraphWidget::set_read_only(bool read_only) {
  if (read_only == read_only_) {
    return;
  }
  read_only_ = read_only;
  drag_index_ = -1;
  setFocusPolicy(read_only ? Qt::NoFocus : Qt::StrongFocus);
  setToolTip(read_only ? QString() : graph_tool_tip());
  update();
}

void ContourGraphWidget::set_selected_point(int index) {
  const auto clamped = std::clamp(index, 0, static_cast<int>(contour_.points.size()) - 1);
  if (clamped == selected_) {
    return;
  }
  selected_ = clamped;
  update();
  if (selection_changed) {
    selection_changed();
  }
}

void ContourGraphWidget::commit(const StyleContour& before, int selected_before) {
  const bool curve_changed = !(before == contour_);
  if (curve_changed) {
    lut_ = build_style_contour_lut(contour_);
  }
  update();
  if (curve_changed && contour_changed) {
    contour_changed();
  }
  if ((curve_changed || selected_before != selected_) && selection_changed) {
    selection_changed();
  }
}

void ContourGraphWidget::set_selected_levels(int input, int output) {
  const auto before = contour_;
  (void)move_contour_point(contour_, static_cast<std::size_t>(selected_), static_cast<float>(input),
                           static_cast<float>(output));
  commit(before, selected_);
}

void ContourGraphWidget::set_selected_corner(bool corner) {
  const auto before = contour_;
  (void)set_contour_point_corner(contour_, static_cast<std::size_t>(selected_), corner);
  commit(before, selected_);
}

bool ContourGraphWidget::can_delete_selected_point() const noexcept {
  return contour_.points.size() > kMinContourPoints;
}

void ContourGraphWidget::delete_selected_point() {
  const auto before = contour_;
  const auto selected_before = selected_;
  if (remove_contour_point(contour_, static_cast<std::size_t>(selected_))) {
    selected_ = std::clamp(selected_, 0, static_cast<int>(contour_.points.size()) - 1);
  }
  commit(before, selected_before);
}

QSize ContourGraphWidget::sizeHint() const { return QSize(300 + 2 * kGraphMargin, 300 + 2 * kGraphMargin); }

QRect ContourGraphWidget::graph_rect() const {
  const auto available = rect().adjusted(kGraphMargin, kGraphMargin, -kGraphMargin, -kGraphMargin);
  const auto side = std::max(2, std::min(available.width(), available.height()));
  return QRect(available.left() + (available.width() - side) / 2, available.top() + (available.height() - side) / 2,
               side, side);
}

QPointF ContourGraphWidget::position_for_levels(double input, double output) const {
  const auto graph = graph_rect();
  return QPointF(graph.left() + std::clamp(input, 0.0, 255.0) * (graph.width() - 1) / 255.0,
                 graph.bottom() - std::clamp(output, 0.0, 255.0) * (graph.height() - 1) / 255.0);
}

double ContourGraphWidget::input_for_x(double x) const {
  const auto graph = graph_rect();
  return std::clamp((x - graph.left()) / std::max(1, graph.width() - 1), 0.0, 1.0) * 255.0;
}

double ContourGraphWidget::output_for_y(double y) const {
  const auto graph = graph_rect();
  return std::clamp((graph.bottom() - y) / std::max(1, graph.height() - 1), 0.0, 1.0) * 255.0;
}

int ContourGraphWidget::hit_point(QPointF position) const {
  int nearest = -1;
  auto nearest_distance = std::numeric_limits<double>::max();
  for (int index = 0; index < static_cast<int>(contour_.points.size()); ++index) {
    const auto& point = contour_.points[static_cast<std::size_t>(index)];
    const auto center = position_for_levels(point.x, point.y);
    const auto dx = position.x() - center.x();
    const auto dy = position.y() - center.y();
    const auto distance = dx * dx + dy * dy;
    if (distance <= kHitRadius * kHitRadius && distance < nearest_distance) {
      nearest = index;
      nearest_distance = distance;
    }
  }
  return nearest;
}

void ContourGraphWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  const auto& colors = theme();
  QPainter painter(this);
  painter.fillRect(rect(), colors.curves_graph_bg);
  const auto graph = graph_rect();
  painter.fillRect(graph, colors.curves_graph_well_bg);

  for (int division = 1; division < 4; ++division) {
    const auto x = graph.left() + division * (graph.width() - 1) / 4;
    const auto y = graph.top() + division * (graph.height() - 1) / 4;
    auto grid = colors.curves_graph_grid;
    grid.setAlpha(division == 2 ? 86 : 50);
    painter.setPen(QPen(grid, 1));
    painter.drawLine(x, graph.top(), x, graph.bottom());
    painter.drawLine(graph.left(), y, graph.right(), y);
  }
  painter.setPen(QPen(colors.curves_graph_diagonal, 1, Qt::DashLine));
  painter.drawLine(graph.bottomLeft(), graph.topRight());

  painter.setRenderHint(QPainter::Antialiasing, true);
  QPainterPath path;
  path.moveTo(position_for_levels(0.0, lut_[0]));
  for (int input = 1; input < 256; ++input) {
    path.lineTo(position_for_levels(input, lut_[static_cast<std::size_t>(input)]));
  }
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(colors.curves_graph_curve_halo, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(path);
  painter.setPen(QPen(colors.curves_graph_ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(path);

  // Corner points are squares, smooth points circles.
  for (int index = 0; index < static_cast<int>(contour_.points.size()); ++index) {
    const auto& point = contour_.points[static_cast<std::size_t>(index)];
    const auto center = position_for_levels(point.x, point.y);
    const bool selected = !read_only_ && index == selected_;
    const auto radius = selected ? 5.5 : 4.2;
    painter.setPen(QPen(selected ? colors.curves_graph_focus : colors.curves_graph_ink, selected ? 2.0 : 1.4));
    painter.setBrush(selected ? colors.curves_graph_point_selected_bg : colors.curves_graph_point_bg);
    if (point.corner) {
      painter.drawRect(QRectF(center.x() - radius, center.y() - radius, 2.0 * radius, 2.0 * radius));
    } else {
      painter.drawEllipse(center, radius, radius);
    }
  }

  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setPen(QPen(hasFocus() ? colors.curves_graph_focus : colors.curves_graph_border, hasFocus() ? 2 : 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(graph.adjusted(0, 0, -1, -1));
}

void ContourGraphWidget::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  update();
}

void ContourGraphWidget::focusOutEvent(QFocusEvent* event) {
  QWidget::focusOutEvent(event);
  update();
}

void ContourGraphWidget::mousePressEvent(QMouseEvent* event) {
  const auto position = event->position();
  if (read_only_ || event->button() != Qt::LeftButton ||
      !graph_rect().adjusted(-kGraphMargin, -kGraphMargin, kGraphMargin, kGraphMargin).contains(position.toPoint())) {
    QWidget::mousePressEvent(event);
    return;
  }
  setFocus(Qt::MouseFocusReason);
  const auto before = contour_;
  const auto selected_before = selected_;
  auto index = hit_point(position);
  if (index < 0) {
    if (const auto added = add_contour_point(contour_, static_cast<float>(input_for_x(position.x())),
                                             static_cast<float>(output_for_y(position.y())));
        added.has_value()) {
      index = static_cast<int>(*added);
    }
  }
  if (index >= 0) {
    selected_ = index;
    drag_index_ = index;
    drag_origin_ = contour_;
  }
  commit(before, selected_before);
  event->accept();
}

void ContourGraphWidget::apply_drag(QPointF position) {
  if (drag_index_ < 0) {
    return;
  }
  const auto before = contour_;
  const auto selected_before = selected_;
  const auto keep_zone = graph_rect().adjusted(-kRemoveDistance, -kRemoveDistance, kRemoveDistance, kRemoveDistance);
  contour_ = drag_origin_;
  if (!keep_zone.contains(position.toPoint()) && contour_.points.size() > kMinContourPoints) {
    (void)remove_contour_point(contour_, static_cast<std::size_t>(drag_index_));
    selected_ = std::clamp(drag_index_, 0, static_cast<int>(contour_.points.size()) - 1);
  } else {
    (void)move_contour_point(contour_, static_cast<std::size_t>(drag_index_),
                             static_cast<float>(input_for_x(position.x())),
                             static_cast<float>(output_for_y(position.y())));
    selected_ = drag_index_;
  }
  commit(before, selected_before);
}

void ContourGraphWidget::mouseMoveEvent(QMouseEvent* event) {
  if (drag_index_ >= 0 && (event->buttons() & Qt::LeftButton) != 0) {
    apply_drag(event->position());
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void ContourGraphWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && drag_index_ >= 0) {
    apply_drag(event->position());
    drag_index_ = -1;
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void ContourGraphWidget::keyPressEvent(QKeyEvent* event) {
  if (read_only_) {
    QWidget::keyPressEvent(event);
    return;
  }
  if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
    delete_selected_point();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
    const auto count = static_cast<int>(contour_.points.size());
    set_selected_point((selected_ + (event->key() == Qt::Key_PageUp ? count - 1 : 1)) % count);
    event->accept();
    return;
  }
  const auto step = (event->modifiers() & Qt::ShiftModifier) != 0 ? 10 : 1;
  int input_delta = 0;
  int output_delta = 0;
  switch (event->key()) {
    case Qt::Key_Left:
      input_delta = -step;
      break;
    case Qt::Key_Right:
      input_delta = step;
      break;
    case Qt::Key_Up:
      output_delta = step;
      break;
    case Qt::Key_Down:
      output_delta = -step;
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
  const auto& point = contour_.points[static_cast<std::size_t>(selected_)];
  set_selected_levels(static_cast<int>(std::lround(point.x)) + input_delta,
                      static_cast<int>(std::lround(point.y)) + output_delta);
  event->accept();
}

std::optional<QString> request_contour_name(QWidget* parent, const QString& title, const QString& initial) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("layerStyleContourNameDialog"));
  dialog.setWindowTitle(title);
  auto* layout = new QVBoxLayout(&dialog);
  auto* label = new QLabel(QObject::tr("Name:"), &dialog);
  layout->addWidget(label);
  auto* name_edit = new QLineEdit(initial, &dialog);
  name_edit->setObjectName(QStringLiteral("contourNameEdit"));
  label->setBuddy(name_edit);
  layout->addWidget(name_edit);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  match_ok_cancel_widths(*buttons);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  // One size. The window manager draws the title in its own font beside its icon
  // and buttons and cuts what does not fit (KDE showed "New ...tour"), so the
  // dialog is at least as wide as the whole title needs; nothing in it would
  // show more after a resize.
  layout->activate();
  const auto hint = dialog.sizeHint();
  const auto width =
      std::max({kContourNameDialogWidth, hint.width(), fit_progress_dialog_title(dialog, title, {}).minimum_width});
  dialog.setFixedSize(width, hint.height());
  name_edit->selectAll();
  name_edit->setFocus();
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  const auto name = name_edit->text().trimmed();
  if (name.isEmpty()) {
    return std::nullopt;
  }
  return name;
}

std::optional<StyleContour> request_contour_edit(QWidget* parent, const StyleContour& initial,
                                                 const std::function<void(const StyleContour&)>& changed,
                                                 ContourLibrary* library, QString* saved_storage_id,
                                                 const QString& title) {
  if (saved_storage_id != nullptr) {
    saved_storage_id->clear();
  }
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("contourEditorDialog"));
  dialog.setWindowTitle(title.isEmpty() ? QObject::tr("Edit Contour") : title);
  auto* layout = new QVBoxLayout(&dialog);
  auto* body = new QHBoxLayout();
  layout->addLayout(body, 1);

  auto* graph = new ContourGraphWidget(&dialog);
  graph->set_contour(initial);
  body->addWidget(graph, 1);

  auto* side = new QVBoxLayout();
  body->addLayout(side);
  auto* form = new QFormLayout();
  side->addLayout(form);
  auto* input = new QSpinBox(&dialog);
  input->setObjectName(QStringLiteral("contourEditorInputSpin"));
  input->setRange(0, 255);
  input->setToolTip(QObject::tr("The selected point's input level, left to right on the graph"));
  configure_dialog_spinbox(input);
  form->addRow(QObject::tr("Input:"), input);
  auto* output = new QSpinBox(&dialog);
  output->setObjectName(QStringLiteral("contourEditorOutputSpin"));
  output->setRange(0, 255);
  output->setToolTip(QObject::tr("The selected point's output level, bottom to top on the graph"));
  configure_dialog_spinbox(output);
  form->addRow(QObject::tr("Output:"), output);
  auto* corner = new QCheckBox(QObject::tr("Corner"), &dialog);
  corner->setObjectName(QStringLiteral("contourEditorCornerCheck"));
  corner->setToolTip(QObject::tr("The curve turns sharply at this point instead of bending through it"));
  form->addRow(QString(), corner);
  auto* delete_point = new QPushButton(QObject::tr("Delete Point"), &dialog);
  delete_point->setObjectName(QStringLiteral("contourEditorDeletePointButton"));
  side->addWidget(delete_point);
  auto* reset = new QPushButton(QObject::tr("Reset"), &dialog);
  reset->setObjectName(QStringLiteral("contourEditorResetButton"));
  reset->setToolTip(QObject::tr("Put the curve back the way it was when this dialog opened"));
  side->addWidget(reset);
  side->addStretch(1);

  auto* hint = new QLabel(
      QObject::tr("Click the graph to add a point. Drag a point to move it, or off the graph to remove it."), &dialog);
  hint->setObjectName(QStringLiteral("contourEditorHint"));
  hint->setWordWrap(true);
  layout->addWidget(hint);

  // Save as New Contour sits at the left of the OK and Cancel row, away from the
  // point tools: it acts on the whole curve.
  auto* footer = new QHBoxLayout();
  layout->addLayout(footer);
  QPushButton* save_as_new = nullptr;
  if (library != nullptr) {
    save_as_new = new QPushButton(QObject::tr("Save as New Contour…"), &dialog);
    save_as_new->setObjectName(QStringLiteral("contourEditorSaveAsNewButton"));
    save_as_new->setToolTip(QObject::tr("Save this curve as a new contour under a name of your choice"));
    save_as_new->setAutoDefault(false);
    footer->addWidget(save_as_new);
  }
  footer->addStretch(1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  match_ok_cancel_widths(*buttons);
  footer->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  const auto starting = graph->contour();
  // The contour Save as New Contour last added to the library.
  QString saved_id;
  StyleContour saved_contour;
  // The value handed back and previewed: the contour just saved while the curve
  // is still that one, the caller's own contour while the curve is as it came,
  // the new points as "Custom" once it differs.
  const auto current_value = [graph, &initial, &starting, &saved_id, &saved_contour]() {
    if (!saved_id.isEmpty() && style_contours_same_shape(graph->contour(), saved_contour)) {
      return saved_contour;
    }
    if (style_contours_same_shape(graph->contour(), starting)) {
      return initial;
    }
    auto edited = graph->contour();
    edited.name = kCustomContourName;
    return edited;
  };
  const auto sync_fields = [graph, input, output, corner, delete_point] {
    const QSignalBlocker block_input(input);
    const QSignalBlocker block_output(output);
    const QSignalBlocker block_corner(corner);
    const auto& points = graph->contour().points;
    const auto index = static_cast<std::size_t>(graph->selected_point());
    if (index < points.size()) {
      input->setValue(static_cast<int>(std::lround(points[index].x)));
      output->setValue(static_cast<int>(std::lround(points[index].y)));
      corner->setChecked(points[index].corner);
    }
    delete_point->setEnabled(graph->can_delete_selected_point());
  };
  sync_fields();
  graph->selection_changed = sync_fields;
  graph->contour_changed = [&changed, current_value] {
    if (changed) {
      changed(current_value());
    }
  };
  QObject::connect(input, &QSpinBox::valueChanged, &dialog,
                   [graph, output](int value) { graph->set_selected_levels(value, output->value()); });
  QObject::connect(output, &QSpinBox::valueChanged, &dialog,
                   [graph, input](int value) { graph->set_selected_levels(input->value(), value); });
  QObject::connect(corner, &QCheckBox::toggled, &dialog, [graph](bool on) { graph->set_selected_corner(on); });
  QObject::connect(delete_point, &QPushButton::clicked, &dialog, [graph] { graph->delete_selected_point(); });
  QObject::connect(reset, &QPushButton::clicked, &dialog, [graph, &starting, sync_fields] {
    const bool differs = !(graph->contour() == starting);
    graph->set_contour(starting);
    sync_fields();
    if (differs && graph->contour_changed) {
      graph->contour_changed();
    }
  });

  if (save_as_new != nullptr) {
    QObject::connect(save_as_new, &QPushButton::clicked, &dialog, [&] {
      const auto title = QObject::tr("New Contour");
      const auto name =
          request_contour_name(&dialog, title, QObject::tr("Contour %1").arg(library->entries().size() + 1U));
      if (!name.has_value()) {
        return;
      }
      const auto id = library->add_contour(*name, graph->contour());
      const auto* entry = library->find_entry(id);
      if (entry == nullptr) {
        show_critical_message(&dialog, title, QObject::tr("Could not save the contour."),
                              QStringLiteral("layerStyleContourErrorDialog"));
        return;
      }
      saved_id = id;
      saved_contour = entry->contour;
      // The editor goes on from the contour as the library holds it (whole levels).
      graph->set_contour(saved_contour);
      sync_fields();
      if (changed) {
        changed(current_value());
      }
    });
  }

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  auto result = current_value();
  if (saved_storage_id != nullptr && !saved_id.isEmpty() &&
      style_contours_same_shape(graph->contour(), saved_contour)) {
    *saved_storage_id = saved_id;
  }
  return result;
}

StyleContour straight_line_contour() {
  if (const auto* linear = find_builtin_contour_preset("contour.linear"); linear != nullptr) {
    return linear->contour;
  }
  StyleContour contour;
  contour.name = "Linear";
  return contour;
}

ContourPickerCombo::ContourPickerCombo(QWidget* parent) : QComboBox(parent) {}

void ContourPickerCombo::hidePopup() {
  if (popup_ != nullptr) {
    popup_->close();
  }
  QComboBox::hidePopup();
}

void ContourPickerCombo::showPopup() {
  if (popup_ != nullptr) {
    popup_->close();
    return;
  }
  if (!isEnabled() || count() == 0 || (dismissed_.isValid() && dismissed_.elapsed() < 250)) {
    return;
  }
  auto* popup = new QFrame(this, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(objectName() + QStringLiteral("Popup"));
  popup->setFrameShape(QFrame::StyledPanel);
  // A top-level popup gets no themed background from the app sheet. The rule
  // selects it by its property, since the three pickers' popups have three names.
  popup->setProperty("contourPickerPopup", true);
  set_themed_style(*popup, QStringLiteral(R"(
    QFrame[contourPickerPopup="true"] {
      background: @field_bg_large;
      border: 1px solid @field_inset_border;
    }
  )"));
  popup_ = popup;
  connect(popup, &QObject::destroyed, this, [this] { dismissed_.start(); });
  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(2, 2, 2, 2);
  layout->setSpacing(0);

  auto* grid = new PresetGridWidget(popup);
  grid->setObjectName(objectName() + QStringLiteral("Grid"));
  grid->setSelectionMode(QAbstractItemView::SingleSelection);
  grid->setIconSize(kGridIconSize);
  grid->set_cell_extent(kGridCellExtent);
  grid->setFrameShape(QFrame::NoFrame);
  // A click on the header opens or closes it: choosing a folder means nothing here.
  grid->set_folder_click_toggles(true);
  // The saved contours' folder key; never shown (the label callback names it).
  static const QString kSavedFolder = QStringLiteral("saved");
  grid->set_folder_label_callback(
      [](const QString&, int count) { return QObject::tr("Saved Contours (%1)").arg(count); });
  // The cells are the combo's rows by index, so a pick is setCurrentIndex.
  grid->set_entries_callback([this] {
    std::vector<PresetTreeEntry> rows;
    bool after_separator = false;
    for (int row = 0; row < count(); ++row) {
      if (itemData(row, Qt::AccessibleDescriptionRole).toString() == QLatin1String("separator")) {
        after_separator = true;
        continue;
      }
      const auto name = itemText(row);
      rows.push_back({QString::number(row), name, itemIcon(row), name, after_separator ? kSavedFolder : QString()});
    }
    return rows;
  });
  grid->reload(QString::number(currentIndex()));
  layout->addWidget(grid, 1);

  const auto pick = [this, popup](const QString& id) {
    bool ok = false;
    const auto row = id.toInt(&ok);
    if (!ok || row < 0 || row >= count()) {
      return;
    }
    popup->close();
    setCurrentIndex(row);
    emit activated(row);
  };
  grid->set_entry_clicked_callback(pick);
  connect(grid, &QListWidget::itemActivated, popup, [pick](QListWidgetItem* item) {
    if (const auto id = PresetGridWidget::item_entry_id(item); !id.isEmpty()) {
      pick(id);
    }
  });

  // kGridColumns cells a row (at least the box's width), as tall as the rows need,
  // up to half the screen; a longer saved list scrolls.
  const int pitch = kGridCellExtent + grid->spacing();
  const int frame = 2 * (popup->frameWidth() + layout->contentsMargins().left());
  const int scroll_bar = grid->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, grid);
  const int width = std::max(this->width(), kGridColumns * pitch + grid->spacing() + scroll_bar + frame + 2);
  const auto* screen = this->screen();
  const int height_cap = screen != nullptr ? std::max(160, screen->availableGeometry().height() / 2) : 480;
  int builtin_cells = 0;
  int saved_cells = 0;
  for (int row = 0; row < grid->count(); ++row) {
    if (!PresetGridWidget::is_folder_item(grid->item(row))) {
      (grid->item(row)->data(PresetGridWidget::kFolderRole).toString().isEmpty() ? builtin_cells : saved_cells) += 1;
    }
  }
  const auto rows_of = [](int cells) { return (cells + kGridColumns - 1) / kGridColumns; };
  const int header = saved_cells > 0 ? std::max(24, grid->fontMetrics().height() + 10) + grid->spacing() : 0;
  const int estimate = grid->spacing() + (rows_of(builtin_cells) + rows_of(saved_cells)) * pitch + header + frame + 2;
  popup->resize(width, std::min(height_cap, estimate));
  position_popup_below(*this, *popup);
  popup->show();
  // A hidden grid lays out against a stale width, so the checks run once it shows.
  // The cells' pitch as the style sheet draws them sets the width for kGridColumns
  // a row (with room kept for a scroll bar), then the height grows until nothing
  // scrolls (or the cap is reached).
  layout->activate();
  grid->doItemsLayout();
  if (grid->count() > 1 && !PresetGridWidget::is_folder_item(grid->item(1))) {
    const auto first = grid->visualItemRect(grid->item(0));
    const auto second = grid->visualItemRect(grid->item(1));
    const int measured_pitch = second.top() == first.top() && second.left() > first.left()
                                   ? second.left() - first.left()
                                   : first.width() + grid->spacing();
    const int chrome = popup->width() - grid->viewport()->width() +
                       (grid->verticalScrollBar()->isVisible() ? 0 : scroll_bar);
    const int wanted = std::max(this->width(), first.left() + kGridColumns * measured_pitch + chrome + 1);
    if (wanted != popup->width()) {
      popup->resize(wanted, popup->height());
      layout->activate();
      grid->doItemsLayout();
    }
  }
  for (int attempt = 0; attempt < 4 && grid->verticalScrollBar()->maximum() > 0 && popup->height() < height_cap;
       ++attempt) {
    popup->resize(popup->width(), std::min(height_cap, popup->height() + grid->verticalScrollBar()->maximum() + 2));
    layout->activate();
    grid->doItemsLayout();
    position_popup_below(*this, *popup);
  }
  grid->setFocus(Qt::PopupFocusReason);
  if (auto* current = grid->currentItem(); current != nullptr) {
    grid->scrollToItem(current);
  }
}

QStringList request_contours_to_save(QWidget* parent, const ContourLibrary& library) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("saveContoursDialog"));
  dialog.setWindowTitle(QObject::tr("Save Contours"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* heading = new QLabel(QObject::tr("Contours to save in the file:"), &dialog);
  layout->addWidget(heading);
  auto* list = new QListWidget(&dialog);
  list->setObjectName(QStringLiteral("saveContoursList"));
  list->setIconSize(QSize(32, 24));
  for (const auto& entry : library.entries()) {
    auto* item = new QListWidgetItem(contour_preset_icon(entry.contour), entry.name, list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    item->setData(Qt::UserRole, entry.storage_id);
  }
  layout->addWidget(list, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  match_ok_cancel_widths(*buttons);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return {};
  }
  QStringList ids;
  for (int row = 0; row < list->count(); ++row) {
    if (list->item(row)->checkState() == Qt::Checked) {
      ids.push_back(list->item(row)->data(Qt::UserRole).toString());
    }
  }
  return ids;
}

QStringList load_contours_from_file(QWidget* parent, ContourLibrary& library, const QString& file_dialog_object_name) {
  const auto title = QObject::tr("Load Contours");
  const auto path = get_open_file_name(parent, title, {}, QObject::tr("Photoshop Contours (*.shc)"), nullptr,
                                       file_dialog_object_name);
  if (path.isEmpty()) {
    return {};
  }
  QString error;
  QStringList warnings;
  const auto ids = library.import_shc(path, error, warnings);
  if (!error.isEmpty()) {
    show_critical_message(parent, title, error, QStringLiteral("layerStyleContourErrorDialog"));
  } else if (!warnings.isEmpty()) {
    show_information_message(parent, title, warnings.join(QLatin1Char('\n')),
                             QStringLiteral("layerStyleContourWarningsDialog"));
  }
  return ids;
}

bool save_contours_to_file(QWidget* parent, const ContourLibrary& library, const QString& file_dialog_object_name) {
  const auto ids = request_contours_to_save(parent, library);
  if (ids.isEmpty()) {
    return false;
  }
  const auto title = QObject::tr("Save Contours");
  auto path = get_save_file_name(parent, title, QObject::tr("Contours.shc"), QObject::tr("Photoshop Contours (*.shc)"),
                                 nullptr, file_dialog_object_name);
  if (path.isEmpty()) {
    return false;
  }
  if (!path.endsWith(QStringLiteral(".shc"), Qt::CaseInsensitive)) {
    path += QStringLiteral(".shc");
  }
  QString error;
  if (!library.export_shc(ids, path, error)) {
    show_critical_message(parent, title, error, QStringLiteral("layerStyleContourErrorDialog"));
    return false;
  }
  offer_browser_download_for_saved_file(path);
  return true;
}

}  // namespace patchy::ui
