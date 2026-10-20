// MainWindow's right-hand panels, split out of main_window.cpp: create_docks builds
// the Layers, Channels, Paths, History, Properties, Info and Palette panels and hands
// them to the PanelColumn (panel_column.hpp) beside the document tabs; the column's
// width (content-fit, picked, snapped), its screen fit, the Window menu panel entries,
// the panel header menu, the saved panel layout and Reset Panels live here too.
// docs/right-docks.md owns the rules.

#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "ui/channel_panel.hpp"
#include "core/document_channel.hpp"
#include "core/document_path.hpp"
#include "ui/main_window.hpp"
#include <QClipboard>
#include <QScrollBar>
#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/modifier_names.hpp"
#include "ui/paths_panel.hpp"

#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/vector_shape.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/palette_panel.hpp"
#include "ui/panel_column.hpp"
#include "ui/scanner_import.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/unit_spin_box.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/update_checker.hpp"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCollator>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLatin1String>
#include <QLayout>
#include <QLayoutItem>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMargins>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QMimeData>
#include <QMouseEvent>
#include <QObject>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QSplitter>
#include <QString>
#include <QStringLiteral>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <qlatin1stringview.h>
#include <utility>
#include <vector>


// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

// The narrowest the column's content-fit width (right_dock_minimum_width_)
// is ever measured at. The user can drag the column narrower, down to
// kRightDockFloorWidth; the panels then scroll horizontally (GitHub issue 96).
constexpr int kRightDockMinimumWidth = 280;
// Hard floor of a user- or script-set column width (the
// patchy.ui.setSidePanelWidth clamp uses the same value).
constexpr int kRightDockFloorWidth = 120;
// A handle drag that ends within this many pixels of the content-fit width
// lands exactly on it, the width with no dead strip and no sideways scroll.
constexpr int kRightDockSnapDistance = 10;
// The layer list's narrowest width: a top-level row with a mask, its link and
// two badges (251 px offscreen) stays whole, badges included. Wider than every
// wrapped row, it sets the Layers panel's compact width (GitHub issue 199).
constexpr int kLayerListMinimumWidth = 256;
// The name filter's narrowest width beside the Lock buttons; below it the
// filter takes a line of its own.
constexpr int kLayerNameFilterMinimumWidth = 90;
// Width the column chrome adds around the Layers panel before it is measured: a
// column at width W gives the panel W minus this.
constexpr int kRightDockChromeWidth = 18;
// Floors are deliberately low: an expanded section's minimum feeds the window's
// minimum height, so tall floors force the whole window to grow. An expand asks
// for the preferred height from its neighbors instead (PanelColumn).
constexpr int kHistoryDockExpandedMinimumHeight = 90;
constexpr int kHistoryDockPreferredHeight = 190;
constexpr int kPropertiesDockMaximumHeight = 230;
constexpr int kPaletteDockPreferredHeight = 320;

// The Layers panel rows that wrap instead of clipping (GitHub issues 96 and 199).
// Each reports two widths: minimumSize() is the narrowest width it fits whole,
// which sets the column's compact width, and comfortable_width() the width the
// column opens at (the old minimum, so the panel opens exactly as before). The
// minimum height follows the line count of the last laid-out width.
class WrappingPanelRowLayout : public QLayout {
public:
  [[nodiscard]] virtual int comfortable_width() const = 0;

  ~WrappingPanelRowLayout() override {
    while (auto* item = WrappingPanelRowLayout::takeAt(0)) {
      delete item;
    }
  }

  void addItem(QLayoutItem* item) override { items_.push_back(item); }
  void add_layout(QLayout* layout) {
    addChildLayout(layout);
    addItem(layout);
  }
  [[nodiscard]] int count() const override { return static_cast<int>(items_.size()); }
  [[nodiscard]] QLayoutItem* itemAt(int index) const override {
    return index >= 0 && index < count() ? items_[static_cast<std::size_t>(index)] : nullptr;
  }
  QLayoutItem* takeAt(int index) override {
    if (index < 0 || index >= count()) {
      return nullptr;
    }
    auto* item = items_[static_cast<std::size_t>(index)];
    items_.erase(items_.begin() + index);
    return item;
  }
  [[nodiscard]] Qt::Orientations expandingDirections() const override { return {}; }
  [[nodiscard]] bool hasHeightForWidth() const override { return true; }

protected:
  WrappingPanelRowLayout() { setContentsMargins(0, 0, 0, 0); }

  [[nodiscard]] QLayoutItem* item(int index) const { return items_[static_cast<std::size_t>(index)]; }
  [[nodiscard]] std::vector<int> visible_items() const {
    std::vector<int> visible;
    for (int index = 0; index < count(); ++index) {
      if (!item(index)->isEmpty()) {
        visible.push_back(index);
      }
    }
    return visible;
  }
  [[nodiscard]] int horizontal_margins() const {
    const auto margins = contentsMargins();
    return margins.left() + margins.right();
  }
  [[nodiscard]] int vertical_margins() const {
    const auto margins = contentsMargins();
    return margins.top() + margins.bottom();
  }
  // Called from setGeometry with the line count it just laid out. A change moves
  // the minimum height, so the enclosing layout is re-requested once this pass is
  // over; the next pass is stable because the wrap depends on the width alone.
  void note_line_count(int lines) {
    if (lines != laid_out_lines_) {
      laid_out_lines_ = lines;
      QTimer::singleShot(0, this, [this] { invalidate(); });
    }
  }
  [[nodiscard]] int laid_out_lines() const { return laid_out_lines_; }

private:
  std::vector<QLayoutItem*> items_;
  int laid_out_lines_{1};
};

// The blend/opacity row and the lock/filter row. Items keep their natural
// widths, packed left. While the row does not fit one line, the first item
// takes a line of its own (the blend combo; the Lock buttons) and the rest pack
// greedily onto the lines below, so its narrowest width is its widest item. A
// stretch item (the name filter) takes the rest of its line and counts as
// stretch_minimum wide when the row decides what fits.
class WrappingControlRowLayout final : public WrappingPanelRowLayout {
public:
  explicit WrappingControlRowLayout(int spacing) { setSpacing(spacing); }

  void set_stretch_widget(QWidget* widget, int minimum_width) {
    stretch_widget_ = widget;
    stretch_minimum_ = minimum_width;
  }

  [[nodiscard]] int heightForWidth(int width) const override {
    return lines_height(line_starts(width - horizontal_margins())) + vertical_margins();
  }
  [[nodiscard]] QSize minimumSize() const override {
    int width = 0;
    for (const auto index : visible_items()) {
      width = std::max(width, item_width(index));
    }
    return {width + horizontal_margins(), laid_out_height() + vertical_margins()};
  }
  [[nodiscard]] QSize sizeHint() const override {
    const auto visible = visible_items();
    return {run_width(visible, 0, visible.size()) + horizontal_margins(),
            line_height(visible, 0, visible.size()) + vertical_margins()};
  }
  // The first item over the rest on one line: the blend row's wrapped width
  // (GitHub issue 96), so long translated prefixes still wrap at the opening
  // width rather than widen the column.
  [[nodiscard]] int comfortable_width() const override {
    const auto visible = visible_items();
    if (visible.empty()) {
      return horizontal_margins();
    }
    return std::max(item_width(visible.front()), run_width(visible, 1, visible.size())) + horizontal_margins();
  }

  void setGeometry(const QRect& rect) override {
    QLayout::setGeometry(rect);
    const auto area = contentsRect();
    const auto visible = visible_items();
    const auto starts = line_starts(area.width());
    auto y = area.y();
    for (std::size_t line = 0; line < starts.size(); ++line) {
      const auto first = starts[line];
      const auto last = line + 1 < starts.size() ? starts[line + 1] : visible.size();
      const auto height = line_height(visible, first, last);
      auto x = area.x();
      for (auto position = first; position < last; ++position) {
        auto* line_item = item(visible[position]);
        const auto hint = line_item->sizeHint();
        const auto remaining = std::max(0, area.right() + 1 - x);
        const auto width = is_stretch(line_item) ? remaining : std::min(hint.width(), remaining);
        line_item->setGeometry(QRect(x, y + (height - hint.height()) / 2, width, hint.height()));
        x += width + spacing();
      }
      y += height + spacing();
    }
    note_line_count(static_cast<int>(std::max<std::size_t>(1, starts.size())));
  }

private:
  [[nodiscard]] bool is_stretch(QLayoutItem* candidate) const {
    return stretch_widget_ != nullptr && candidate->widget() == stretch_widget_;
  }
  [[nodiscard]] int item_width(int index) const {
    auto* candidate = item(index);
    return is_stretch(candidate) ? std::max(stretch_minimum_, candidate->minimumSize().width())
                                 : candidate->sizeHint().width();
  }
  // Natural width of the visible items [first, last) on one line.
  [[nodiscard]] int run_width(const std::vector<int>& visible, std::size_t first, std::size_t last) const {
    int width = 0;
    for (auto position = first; position < last; ++position) {
      width += item_width(visible[position]);
    }
    return width + static_cast<int>(last > first ? last - first - 1 : 0) * spacing();
  }
  [[nodiscard]] int line_height(const std::vector<int>& visible, std::size_t first, std::size_t last) const {
    int height = 0;
    for (auto position = first; position < last; ++position) {
      height = std::max(height, item(visible[position])->sizeHint().height());
    }
    return height;
  }
  // Positions in visible_items() where each line starts at this content width.
  [[nodiscard]] std::vector<std::size_t> line_starts(int width) const {
    const auto visible = visible_items();
    if (visible.empty()) {
      return {};
    }
    if (run_width(visible, 0, visible.size()) <= width) {
      return {0};
    }
    std::vector<std::size_t> starts{0};
    int used = 0;
    for (std::size_t position = 1; position < visible.size(); ++position) {
      const auto extent = item_width(visible[position]);
      if (position == 1 || used + spacing() + extent > width) {
        starts.push_back(position);
        used = extent;
      } else {
        used += spacing() + extent;
      }
    }
    return starts;
  }
  [[nodiscard]] int lines_height(const std::vector<std::size_t>& starts) const {
    const auto visible = visible_items();
    int height = 0;
    for (std::size_t line = 0; line < starts.size(); ++line) {
      const auto last = line + 1 < starts.size() ? starts[line + 1] : visible.size();
      height += line_height(visible, starts[line], last);
    }
    return height + static_cast<int>(starts.empty() ? 0 : starts.size() - 1) * spacing();
  }
  // The height at the line count last laid out: the first line, then each
  // wrapped line at the tallest item after the first.
  [[nodiscard]] int laid_out_height() const {
    const auto visible = visible_items();
    if (visible.empty()) {
      return 0;
    }
    const auto lines = laid_out_lines();
    if (lines <= 1) {
      return line_height(visible, 0, visible.size());
    }
    return line_height(visible, 0, 1) + (lines - 1) * (line_height(visible, 1, visible.size()) + spacing());
  }

  QWidget* stretch_widget_{nullptr};
  int stretch_minimum_{0};
};

// The Layers panel footer (GitHub issue 199). The buttons sit at their maximum
// width in one row while it fits, the width the column opens at. Narrower, they
// share the row evenly down to their minimum width (the theme's
// layerActionButton rule sets both), then wrap into balanced rows (eight
// buttons: four over four), so the footer never holds the column wider.
class WrappingButtonRowLayout final : public WrappingPanelRowLayout {
public:
  WrappingButtonRowLayout(int natural_spacing, int compact_spacing)
      : natural_spacing_(natural_spacing), compact_spacing_(compact_spacing) {
    setSpacing(natural_spacing);
  }

  [[nodiscard]] int heightForWidth(int width) const override {
    return rows_height(arrange(width - horizontal_margins()).rows) + vertical_margins();
  }
  [[nodiscard]] QSize minimumSize() const override {
    // Two rows bound the narrowest width; the other panel rows are wider anyway.
    const auto columns = (static_cast<int>(visible_items().size()) + 1) / 2;
    const auto width = columns * compact_width() + std::max(0, columns - 1) * compact_spacing_;
    return {width + horizontal_margins(), rows_height(laid_out_lines()) + vertical_margins()};
  }
  [[nodiscard]] QSize sizeHint() const override { return {comfortable_width(), rows_height(1) + vertical_margins()}; }
  [[nodiscard]] int comfortable_width() const override {
    const auto buttons = static_cast<int>(visible_items().size());
    return buttons * natural_width() + std::max(0, buttons - 1) * natural_spacing_ + horizontal_margins();
  }

  void setGeometry(const QRect& rect) override {
    QLayout::setGeometry(rect);
    const auto area = contentsRect();
    const auto visible = visible_items();
    const auto arrangement = arrange(area.width());
    const auto height = button_height();
    for (std::size_t position = 0; position < visible.size(); ++position) {
      const auto column = static_cast<int>(position) % arrangement.columns;
      const auto row = static_cast<int>(position) / arrangement.columns;
      item(visible[position])
          ->setGeometry(QRect(area.x() + column * (arrangement.button_width + arrangement.spacing),
                              area.y() + row * (height + natural_spacing_), arrangement.button_width, height));
    }
    note_line_count(std::max(1, arrangement.rows));
  }

private:
  struct Arrangement {
    int columns{1};
    int rows{0};
    int button_width{0};
    int spacing{0};
  };

  [[nodiscard]] int button_height() const {
    int height = 0;
    for (const auto index : visible_items()) {
      height = std::max(height, item(index)->sizeHint().height());
    }
    return height;
  }
  // The narrowest maximum and the widest minimum width among the buttons.
  [[nodiscard]] int natural_width() const {
    int width = 0;
    for (const auto index : visible_items()) {
      const auto extent = item(index)->maximumSize().width();
      width = width == 0 ? extent : std::min(width, extent);
    }
    return std::max(width, compact_width());
  }
  [[nodiscard]] int compact_width() const {
    int width = 0;
    for (const auto index : visible_items()) {
      width = std::max(width, item(index)->minimumSize().width());
    }
    return width;
  }
  [[nodiscard]] int rows_height(int rows) const {
    return rows * button_height() + std::max(0, rows - 1) * natural_spacing_;
  }
  [[nodiscard]] Arrangement arrange(int width) const {
    const auto buttons = static_cast<int>(visible_items().size());
    if (buttons == 0) {
      return {};
    }
    const auto natural = natural_width();
    const auto compact = compact_width();
    if (buttons * natural + (buttons - 1) * natural_spacing_ <= width) {
      return {buttons, 1, natural, natural_spacing_};
    }
    // The fewest rows whose columns still fit at the compact width; one column
    // at the compact width when nothing does (the panel scrolls sideways then).
    for (int rows = 1; rows < buttons; ++rows) {
      const auto columns = (buttons + rows - 1) / rows;
      const auto button_width = (width - (columns - 1) * compact_spacing_) / columns;
      if (button_width >= compact) {
        return {columns, (buttons + columns - 1) / columns, std::min(button_width, natural), compact_spacing_};
      }
    }
    return {1, buttons, compact, compact_spacing_};
  }

  int natural_spacing_;
  int compact_spacing_;
};

}  // namespace

void MainWindow::apply_right_column_width(int width) {
  right_dock_pinned_width_ = width;
  if (workspace_splitter_ == nullptr || panel_column_ == nullptr) {
    return;
  }
  // The splitter keeps the column at this width when the window resizes (the
  // column's stretch factor is 0), so one call holds it.
  const auto total = workspace_splitter_->width() > 0 ? workspace_splitter_->width() : this->width();
  const auto rest = std::max(0, total - workspace_splitter_->handleWidth() - width);
  workspace_splitter_->setSizes({rest, width});
}

void MainWindow::set_right_dock_stack_width(int width) {
  // Re-measure lazily: styling and retranslation both change the layers
  // panel's minimum after the column is built.
  right_dock_width_follows_minimum_ = false;
  update_right_dock_minimum_width();
  // Any width the user or a script picks is kept, narrower than the content
  // included (the panels scroll horizontally then).
  const auto max_width = std::max(kRightDockFloorWidth, this->width() - 260);
  apply_right_column_width(std::clamp(width, kRightDockFloorWidth, max_width));
}

void MainWindow::handle_right_column_moved() {
  if (panel_column_ == nullptr) {
    return;
  }
  // Measured with following off, so the measurement cannot pin the column back
  // to the content-fit width under the user's drag.
  right_dock_width_follows_minimum_ = false;
  update_right_dock_minimum_width();
  const auto width = panel_column_->width();
  right_dock_pinned_width_ = width;
  // Near the content-fit width the drag snaps to it, and the column follows that
  // width again (language switch, restyle) as it did before any drag.
  if (right_dock_minimum_width_ > 0 && std::abs(width - right_dock_minimum_width_) <= kRightDockSnapDistance) {
    right_dock_width_follows_minimum_ = true;
    if (width != right_dock_minimum_width_) {
      apply_right_column_width(right_dock_minimum_width_);
    }
  }
}

void MainWindow::fit_right_dock_floors_to_screen() {
  const int budget = available_window_height();
  if (budget <= 0 || layout() == nullptr || panel_column_ == nullptr) {
    return;
  }
  // The Layers panel's fixed 240 px floor overrides its layout's minimum, so on
  // font metrics where the content needs more (241 px on macOS) the panel could be
  // squeezed below its own controls. Keep the floor at least that minimum.
  if (auto* layers_panel = findChild<QWidget*>(QStringLiteral("layersPanel"));
      layers_panel != nullptr && layers_panel->layout() != nullptr && layers_panel->minimumHeight() > 0) {
    const int content_floor = layers_panel->layout()->minimumSize().height();
    if (layers_panel->minimumHeight() < content_floor) {
      layers_panel->setMinimumHeight(content_floor);
      layers_panel->updateGeometry();
    }
  }
  // The workspace splitter does not pass the column's size changes on to the window's
  // layout by itself; without this the minimum read below (and the activation at the
  // end) would use a stale column size.
  const auto overflow = [this, budget] {
    panel_column_->updateGeometry();
    workspace_splitter_->updateGeometry();
    layout()->invalidate();
    return minimumSizeHint().height() - budget;
  };
  if (const auto over = overflow(); over > 0) {
    panel_column_->lower_floors(over);
  }
  // The Layers panel's own floors (the panel's 240 px, the list's 120 px) sit under
  // its section's; a couple of rows still show on the shortest screens.
  const auto lower_explicit_floor = [](QWidget* widget, int by, int hard_floor) {
    if (widget == nullptr || widget->minimumHeight() <= 0 || by <= 0) {
      return;
    }
    const int layout_floor = widget->layout() != nullptr ? widget->layout()->minimumSize().height() : 0;
    widget->setMinimumHeight(std::max({hard_floor, layout_floor, widget->minimumHeight() - by}));
    widget->updateGeometry();
  };
  if (const auto over = overflow(); over > 0) {
    lower_explicit_floor(findChild<QWidget*>(QStringLiteral("layerList")), over, 48);
  }
  if (const auto over = overflow(); over > 0) {
    lower_explicit_floor(findChild<QWidget*>(QStringLiteral("layersPanel")), over, 0);
  }
  overflow();
  if (isVisible()) {
    // The window's explicit minimum is the layout's total from its last activation;
    // refresh it now so a resize right after sees the lowered floors. Only once
    // shown: an activation before the stylesheet lands would pin an unstyled,
    // taller minimum on the window.
    layout()->activate();
  }
}

void MainWindow::reapply_right_dock_stack_width() {
  if (right_dock_pinned_width_ > 0) {
    apply_right_column_width(right_dock_pinned_width_);
  }
}

void MainWindow::update_right_dock_minimum_width() {
  const auto* layers_panel = findChild<QWidget*>(QStringLiteral("layersPanel"));
  const auto* panel_layout = layers_panel != nullptr ? layers_panel->layout() : nullptr;
  if (panel_layout == nullptr || panel_column_ == nullptr) {
    return;
  }
  if (blend_combo_ != nullptr) {
    // Under the app stylesheet the popup list is sized to the field, and the
    // open list spends part of that width on its frame and scroll bar, so the
    // field needs that much slack for the longest mode name not to elide in
    // the list. (Widening only the view does nothing: the styled popup
    // geometry ignores the view's minimum.)
    const auto scroll_extent =
        blend_combo_->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, blend_combo_);
    blend_combo_->setMinimumWidth(blend_combo_->minimumSizeHint().width() + scroll_extent + 8);
  }
  // The chrome around the panel (frames, the scroll area) can only be trusted once
  // the column has been laid out. The panel sits in RightDockOverflowArea's viewport
  // and can be wider than it while the column is narrowed, so the chrome is the
  // column minus the viewport, not minus the panel.
  const auto* layers_viewport = layers_panel->parentWidget();
  if (layers_viewport != nullptr && layers_panel->isVisible() && layers_viewport->width() > 0 &&
      panel_column_->width() >= layers_viewport->width()) {
    right_dock_chrome_width_ = std::min(panel_column_->width() - layers_viewport->width(), 64);
  }
  const auto chrome = right_dock_chrome_width_ >= 0 ? right_dock_chrome_width_ : kRightDockChromeWidth;
  // The panel layout's minimum is every row fully wrapped: below it the panel
  // scrolls sideways (GitHub issue 199).
  right_dock_compact_width_ = std::max(kRightDockFloorWidth, panel_layout->minimumSize().width() + chrome);
  // The content-fit width takes each wrapping row at its comfortable width: the
  // footer buttons on one row and the blend row with Opacity and Fill on one
  // line under the combo, whose spin boxes re-measure for the translated
  // prefixes, so a column at this width clips neither the Fill spin box nor
  // the popup chevrons.
  int content_width = 0;
  for (int index = 0; index < panel_layout->count(); ++index) {
    auto* item = panel_layout->itemAt(index);
    const auto* row = dynamic_cast<const WrappingPanelRowLayout*>(item->layout());
    content_width = std::max(content_width, row != nullptr ? row->comfortable_width() : item->minimumSize().width());
  }
  const auto margins = panel_layout->contentsMargins();
  content_width += margins.left() + margins.right();
  const auto minimum = std::max(kRightDockMinimumWidth, content_width + chrome);
  if (minimum == right_dock_minimum_width_) {
    return;
  }
  right_dock_minimum_width_ = minimum;
  // Until the user or a script picks a width, the column sits at the narrowest
  // width every panel fits without scrolling, and follows it when a language
  // switch or restyle changes it (GitHub issue 96). A picked width is kept even
  // below that; the panels scroll horizontally then.
  if (right_dock_width_follows_minimum_) {
    apply_right_column_width(minimum);
  }
}

bool MainWindow::handle_right_dock_resize_event(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::LayoutRequest && layer_list_ != nullptr && watched == layer_list_->parentWidget()) {
    // A narrow column wraps the Layers panel's rows (GitHub issue 199), which
    // raises the panel's minimum height. When that pushes the window's minimum
    // past the screen, refit the floors. Only then: the refit also raises the
    // panel's own floor to the wrapped height.
    const auto* panel_layout = layer_list_->parentWidget()->layout();
    const auto height = panel_layout != nullptr ? panel_layout->minimumSize().height() : 0;
    const auto grew = height > layers_panel_minimum_height_;
    layers_panel_minimum_height_ = height;
    if (grew && isVisible()) {
      QTimer::singleShot(0, this, [this] {
        const auto budget = available_window_height();
        if (budget <= 0 || layout() == nullptr || panel_column_ == nullptr || workspace_splitter_ == nullptr) {
          return;
        }
        // The splitter does not pass the column's minimum on to the window's
        // layout by itself (see fit_right_dock_floors_to_screen).
        panel_column_->updateGeometry();
        workspace_splitter_->updateGeometry();
        layout()->invalidate();
        if (minimumSizeHint().height() > budget) {
          fit_right_dock_floors_to_screen();
        }
      });
    }
    return false;
  }
  if (panel_column_ == nullptr || watched != panel_column_ || event->type() != QEvent::Show) {
    return false;
  }
  // The column chrome can only be measured once it is laid out, and the column
  // follows the content-fit width it yields. Re-applied even when the width did
  // not change: before the first show the splitter had no width to divide.
  QTimer::singleShot(0, this, [this] {
    update_right_dock_minimum_width();
    reapply_right_dock_stack_width();
    fit_right_dock_floors_to_screen();
  });
  return false;
}

namespace {

// The property naming the panel a Window menu entry toggles.
constexpr char kWindowPanelIdProperty[] = "patchy.windowPanelId";

}  // namespace

void MainWindow::show_panel(const QString& panel_id) {
  if (panel_column_ == nullptr || !panel_column_->has_panel(panel_id)) {
    return;
  }
  panel_column_->set_panel_visible(panel_id, true);
  panel_column_->bring_to_front(panel_id);
  panel_column_->set_panel_expanded(panel_id, true);
}

void MainWindow::toggle_window_panel(const QString& panel_id) {
  if (panel_column_ == nullptr || !panel_column_->has_panel(panel_id)) {
    return;
  }
  // A checked entry hides its panel, collapsed or not (GitHub issue 234; Photoshop
  // expands a collapsed panel instead). A tab behind another one of its group still
  // comes to the front.
  if (panel_column_->panel_visible(panel_id) && panel_column_->panel_in_front(panel_id)) {
    panel_column_->set_panel_visible(panel_id, false);
    return;
  }
  show_panel(panel_id);
}

void MainWindow::install_window_panel_entries() {
  auto* window_menu = menuBar()->findChild<QMenu*>(QStringLiteral("windowMenu"));
  if (window_menu == nullptr || window_panels_separator_ == nullptr || panel_column_ == nullptr) {
    return;
  }
  struct PanelEntry {
    const char* panel_id;
    const char* action_name;
    const char* title;
  };
  // The panels' own title sources, so each entry shares its panel's translation.
  static constexpr PanelEntry kPanels[] = {
      {"layersDock", "windowPanelLayersAction", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Layers")},
      {"channelsDock", "windowPanelChannelsAction", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Channels")},
      {"pathsDock", "windowPanelPathsAction", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Paths")},
      {"historyDock", "windowPanelHistoryAction", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "History")},
      {"propertiesDock", "windowPanelPropertiesAction",
       QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Properties")},
      {"infoDock", "windowPanelInfoAction", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Info")},
      {"paletteDock", "windowPanelPaletteAction", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Palette")},
  };
  for (const auto& panel : kPanels) {
    const auto id = QString::fromLatin1(panel.panel_id);
    if (!panel_column_->has_panel(id)) {
      continue;
    }
    auto* action = new QAction(window_menu);
    action->setObjectName(QLatin1String(panel.action_name));
    action->setCheckable(true);
    action->setChecked(panel_column_->panel_visible(id));
    action->setMenuRole(QAction::NoRole);
    action->setProperty(kWorksWithoutDocumentProperty, true);
    action->setProperty(kWindowPanelIdProperty, id);
    bind_action_text(action, panel.title);
    connect(action, &QAction::triggered, this, [this, action, id] {
      toggle_window_panel(id);
      action->setChecked(panel_column_->panel_visible(id));
    });
    window_panel_actions_.push_back(action);
  }
  for (const auto* toolbar_name : {"toolPalette", "Options"}) {
    if (auto* toolbar = findChild<QToolBar*>(QLatin1String(toolbar_name)); toolbar != nullptr) {
      window_panel_actions_.push_back(toolbar->toggleViewAction());
    }
  }
  // Window > Reset Panels closes the list: every panel and toolbar back to the layout a
  // first launch has (reset_panels).
  window_reset_panels_separator_ = new QAction(window_menu);
  window_reset_panels_separator_->setSeparator(true);
  window_reset_panels_action_ = new QAction(window_menu);
  window_reset_panels_action_->setObjectName(QStringLiteral("windowResetPanelsAction"));
  window_reset_panels_action_->setMenuRole(QAction::NoRole);
  window_reset_panels_action_->setProperty(kWorksWithoutDocumentProperty, true);
  bind_action_text(window_reset_panels_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Reset Panels"));
  register_hotkey(window_reset_panels_action_, "window.reset_panels", QKeySequence());
  connect(window_reset_panels_action_, &QAction::triggered, this, [this] { reset_panels(); });
  sort_window_panel_entries();
  connect(window_menu, &QMenu::aboutToShow, this, [this] { sort_window_panel_entries(); });
}

std::vector<QAction*> MainWindow::sorted_window_panel_actions() const {
  // Alphabetical in the current language, like Photoshop's panel list.
  QCollator collator;
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  auto sorted = window_panel_actions_;
  std::sort(sorted.begin(), sorted.end(), [&collator](const QAction* left, const QAction* right) {
    return collator.compare(clean_action_text(left), clean_action_text(right)) < 0;
  });
  return sorted;
}

void MainWindow::sort_window_panel_entries() {
  auto* window_menu = menuBar()->findChild<QMenu*>(QStringLiteral("windowMenu"));
  if (window_menu == nullptr || window_documents_separator_ == nullptr || panel_column_ == nullptr) {
    return;
  }
  // The menu reads the order each time it opens, so a language switch re-sorts it.
  for (auto* action : sorted_window_panel_actions()) {
    window_menu->removeAction(action);
    window_menu->insertAction(window_documents_separator_, action);
    if (const auto id = action->property(kWindowPanelIdProperty).toString(); !id.isEmpty()) {
      action->setChecked(panel_column_->panel_visible(id));
    }
  }
  for (auto* action : {window_reset_panels_separator_, window_reset_panels_action_}) {
    if (action != nullptr) {
      window_menu->removeAction(action);
      window_menu->insertAction(window_documents_separator_, action);
    }
  }
}

void MainWindow::show_panel_header_menu(const QString& panel_id, const QPoint& global_position,
                                        bool on_tab_bar) {
  if (panel_column_ == nullptr || !panel_column_->has_panel(panel_id)) {
    return;
  }
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("panelHeaderMenu"));
  const bool expanded = panel_column_->panel_expanded(panel_id);
  auto* collapse = menu.addAction(expanded ? tr("Collapse Panel") : tr("Expand Panel"));
  collapse->setObjectName(QStringLiteral("panelHeaderCollapseAction"));
  connect(collapse, &QAction::triggered, this,
          [this, panel_id, expanded] { panel_column_->set_panel_expanded(panel_id, !expanded); });
  // On a group's tab bar the menu speaks for the whole group, as Photoshop's does.
  const bool whole_group = on_tab_bar && panel_column_->in_tab_group(panel_id);
  auto* close = menu.addAction(whole_group ? tr("Close Tab Group") : tr("Close Panel"));
  close->setObjectName(QStringLiteral("panelHeaderCloseAction"));
  connect(close, &QAction::triggered, this, [this, panel_id, whole_group] {
    const auto ids = whole_group ? panel_column_->section_panel_ids(panel_id) : QStringList{panel_id};
    for (const auto& id : ids) {
      panel_column_->set_panel_visible(id, false);
    }
  });
  menu.addSeparator();
  sort_window_panel_entries();
  for (auto* action : sorted_window_panel_actions()) {
    menu.addAction(action);
  }
  if (window_reset_panels_action_ != nullptr) {
    menu.addSeparator();
    menu.addAction(window_reset_panels_action_);
  }
  menu.exec(global_position);
}

namespace {

// The saved panel layout (docs/right-docks.md, "Saved panel layout"). Every key is a
// compatibility contract. The version guards what the values mean: a release that adds,
// renames or regroups panels bumps it, and an older record is then ignored rather than
// half applied.
constexpr int kPanelLayoutVersion = 2;
QString panel_layout_version_key() { return QStringLiteral("window/panelLayoutVersion"); }
QString hidden_panels_key() { return QStringLiteral("window/hiddenPanels"); }
QString collapsed_panels_key() { return QStringLiteral("window/collapsedPanels"); }
QString front_panel_key() { return QStringLiteral("window/frontPanel"); }
QString panel_column_width_key() { return QStringLiteral("window/panelColumnWidth"); }
QString panel_heights_key() { return QStringLiteral("window/panelSplitterState"); }
QString panel_order_key() { return QStringLiteral("window/panelOrder"); }

// The two toolbars, by object name, with the area each starts in.
struct DefaultToolbar {
  const char* name;
  Qt::ToolBarArea area;
};
constexpr DefaultToolbar kDefaultToolbars[] = {{"toolPalette", Qt::LeftToolBarArea},
                                                {"Options", Qt::TopToolBarArea}};

// The tab group's first panel, which a reset brings to the front.
constexpr char kFirstPanel[] = "layersDock";

}  // namespace

bool MainWindow::panel_layout_persists() {
  // Offscreen runs (the UI tests, --headless scripts, the MCP connector's own window)
  // neither read nor write the user's layout: a test that closes a panel must not leave
  // it closed for the next one, and a headless run must not change the desktop layout.
  return QGuiApplication::platformName().compare(QStringLiteral("offscreen"), Qt::CaseInsensitive) != 0;
}

QStringList MainWindow::collapsed_panel_names() {
  QStringList names;
  if (panel_column_ == nullptr) {
    return names;
  }
  for (const auto& id : panel_column_->panel_ids()) {
    if (!panel_column_->panel_expanded(id)) {
      names << id;
    }
  }
  return names;
}

void MainWindow::apply_collapsed_panels(const QStringList& collapsed) {
  if (panel_column_ == nullptr) {
    return;
  }
  // One call per section: a tab group follows its first panel.
  QStringList done;
  for (const auto& id : panel_column_->panel_ids()) {
    if (done.contains(id)) {
      continue;
    }
    panel_column_->set_panel_expanded(id, !collapsed.contains(id));
    done << panel_column_->section_panel_ids(id);
  }
}

void MainWindow::save_panel_layout() {
  if (panel_layout_persists()) {
    write_panel_layout();
  }
}

void MainWindow::write_panel_layout() {
  if (panel_column_ == nullptr) {
    return;
  }
  auto settings = app_settings();
  QStringList hidden;
  QString front;
  for (const auto& id : panel_column_->panel_ids()) {
    if (!panel_column_->panel_visible(id)) {
      hidden << id;
    } else if (front.isEmpty() && panel_column_->in_tab_group(id) && panel_column_->panel_in_front(id)) {
      front = id;
    }
  }
  for (const auto& toolbar : kDefaultToolbars) {
    if (auto* bar = findChild<QToolBar*>(QLatin1String(toolbar.name)); bar != nullptr && bar->isHidden()) {
      hidden << QLatin1String(toolbar.name);
    }
  }
  settings.setValue(panel_layout_version_key(), kPanelLayoutVersion);
  settings.setValue(hidden_panels_key(), hidden);
  settings.setValue(collapsed_panels_key(), collapsed_panel_names());
  settings.setValue(front_panel_key(), front);
  settings.setValue(panel_order_key(), panel_column_->section_order());
  settings.setValue(panel_heights_key(), panel_column_->save_heights());
  if (right_dock_width_follows_minimum_ || right_dock_pinned_width_ <= 0) {
    settings.remove(panel_column_width_key());
  } else {
    settings.setValue(panel_column_width_key(), right_dock_pinned_width_);
  }
}

void MainWindow::restore_panel_layout() {
  if (panel_layout_persists()) {
    read_panel_layout();
  }
}

void MainWindow::read_panel_layout() {
  const auto settings = app_settings();
  if (panel_column_ == nullptr || settings.value(panel_layout_version_key(), 0).toInt() != kPanelLayoutVersion) {
    return;
  }
  apply_collapsed_panels(settings.value(collapsed_panels_key()).toStringList());
  const auto hidden = settings.value(hidden_panels_key()).toStringList();
  for (const auto& id : panel_column_->panel_ids()) {
    if (hidden.contains(id)) {
      panel_column_->set_panel_visible(id, false);
    }
  }
  if (const auto front = settings.value(front_panel_key()).toString(); panel_column_->has_panel(front)) {
    panel_column_->bring_to_front(front);
  }
  for (const auto& toolbar : kDefaultToolbars) {
    if (auto* bar = findChild<QToolBar*>(QLatin1String(toolbar.name));
        bar != nullptr && hidden.contains(QLatin1String(toolbar.name))) {
      bar->hide();
    }
  }
  // The order first: the heights state is by position. A record without one (saved
  // before panels could move) keeps the default order.
  if (const auto order = settings.value(panel_order_key()).toStringList(); !order.isEmpty()) {
    panel_column_->set_section_order(order);
  }
  panel_column_->restore_heights(settings.value(panel_heights_key()).toByteArray());
  // A picked width; without one the column keeps following the content-fit width. The
  // window's own size is restored first, so the clamp sees the real width.
  if (const auto width = settings.value(panel_column_width_key(), 0).toInt(); width > 0) {
    set_right_dock_stack_width(width);
  }
}

void MainWindow::reset_panels() {
  if (panel_column_ == nullptr) {
    return;
  }
  // Every panel open in the first-launch order, collapse states and heights, Layers in
  // front. Nothing is re-parented, so the window keeps its size and place.
  // Floors a short screen lowered stay lowered: putting them back would ask for more
  // height than the window has and grow it past the screen.
  for (const auto& id : panel_column_->panel_ids()) {
    panel_column_->set_panel_visible(id, true);
  }
  panel_column_->set_section_order(default_panel_order_);
  apply_collapsed_panels(default_collapsed_panels_);
  panel_column_->bring_to_front(QString::fromLatin1(kFirstPanel));
  for (const auto& toolbar : kDefaultToolbars) {
    if (auto* bar = findChild<QToolBar*>(QLatin1String(toolbar.name)); bar != nullptr) {
      if (toolBarArea(bar) != toolbar.area || bar->isFloating()) {
        addToolBar(toolbar.area, bar);
      }
      bar->show();
    }
  }
  panel_column_->apply_preferred_heights();
  // The column follows the content-fit width again, as on a first launch.
  right_dock_width_follows_minimum_ = true;
  right_dock_minimum_width_ = 0;
  update_right_dock_minimum_width();
  fit_right_dock_floors_to_screen();
  sort_window_panel_entries();
}

void MainWindow::create_docks() {
  // The panels are built first and handed to the column at the end: the Layers,
  // Channels and Paths tab group, then one section per remaining panel.
  std::vector<PanelSpec> tab_specs;
  std::vector<PanelSpec> panel_specs;
  auto* layers_panel = new QWidget(this);
  layers_panel->setObjectName(QStringLiteral("layersPanel"));
  layers_panel->setMinimumHeight(240);
  // Its layout requests tell handle_right_dock_resize_event when wrapped rows
  // raise the panel's minimum height (GitHub issue 199).
  layers_panel->installEventFilter(this);
  auto* layers_layout = new QVBoxLayout(layers_panel);
  layers_layout->setContentsMargins(6, 6, 6, 6);
  layers_layout->setSpacing(6);

  auto* layer_list = new LayerListWidget(layers_panel);
  layer_list->set_drop_finished_callback([this] { handle_layer_drop(); });
  layer_list->set_file_drop_paths_callback(
      [this](const QMimeData* mime_data) { return supported_layer_drop_paths(mime_data); });
  layer_list->set_drag_blocked_callback([this] {
    show_status_error(tr("Clear the layer name filter to reorder layers"));
  });
  layer_list->set_clip_boundary_callbacks(
      [this](LayerId upper, LayerId lower) {
        if (!has_active_document()) {
          return false;
        }
        const auto& doc = std::as_const(document());
        const auto location = find_layer_location(doc.layers(), upper);
        if (!location.has_value() || location->siblings == nullptr) {
          return false;
        }
        const auto& siblings = *location->siblings;
        const auto index = location->index;
        const auto& layer = siblings[index];
        if (layer.kind() == LayerKind::Group) {
          return false;
        }
        // Only the boundary between adjacent siblings is a clip boundary; the
        // row visually below can also be a group header or another parent's layer.
        if (index == 0 || siblings[index - 1].id() != lower) {
          return false;
        }
        return layer.clipped() || effective_clip_base(siblings, index) != nullptr;
      },
      [this](LayerId upper) {
        if (!has_active_document() || document().find_layer(upper) == nullptr) {
          return;
        }
        reveal_layer_in_layer_list(upper);
        if (document().active_layer_id() != upper) {
          document().set_active_layer(upper);
        }
        toggle_active_layer_clipping();
      });
  layer_list->set_visibility_isolate_callback([this](LayerId id) { isolate_layer_visibility(id); });
  layer_list->set_visibility_sweep_callback(
      [this](LayerId id, bool visible) { set_layer_visibility(id, visible); });
  layer_list->set_ctrl_click_callback([this](QListWidgetItem* item, LayerCtrlClickTarget target) {
    if (canvas_ == nullptr || item == nullptr) {
      return;
    }
    const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    if (target == LayerCtrlClickTarget::MaskThumbnail) {
      canvas_->select_layer_mask_pixels(id);
    } else if (target == LayerCtrlClickTarget::VectorMaskThumbnail) {
      const auto* layer = std::as_const(document()).find_layer(id);
      const auto* mask = layer != nullptr ? layer->vector_mask() : nullptr;
      if (mask != nullptr) {
        // Materialize the coverage cache onto the whole canvas (zero outside
        // cache_bounds) so the selection matches the rendered mask.
        PixelBuffer coverage(document().width(), document().height(), PixelFormat::gray8());
        for (int y = 0; y < coverage.height(); ++y) {
          for (int x = 0; x < coverage.width(); ++x) {
            const auto local_x = x - mask->cache_bounds.x;
            const auto local_y = y - mask->cache_bounds.y;
            std::uint8_t value = 0;
            if (!mask->cache.empty() && local_x >= 0 && local_y >= 0 &&
                local_x < mask->cache.width() && local_y < mask->cache.height()) {
              value = *mask->cache.pixel(local_x, local_y);
            }
            *coverage.pixel(x, y) = value;
          }
        }
        canvas_->replace_selection_from_grayscale(coverage, tr("Load vector mask selection"));
        statusBar()->showMessage(tr("Loaded the vector mask as a selection"));
      }
    } else if (target == LayerCtrlClickTarget::SmartFilterMaskThumbnail) {
      const auto* layer = std::as_const(document()).find_layer(id);
      const auto* stack = layer != nullptr ? layer->smart_filter_stack() : nullptr;
      const auto pixels = stack != nullptr
                              ? materialize_smart_filter_mask(
                                    stack->mask, document().width(),
                                    document().height())
                              : std::nullopt;
      if (pixels.has_value()) {
        canvas_->replace_selection_from_grayscale(
            *pixels, tr("Load Smart Filter mask selection"));
        statusBar()->showMessage(
            tr("Loaded the Smart Filter mask as a selection"));
      }
    } else {
      canvas_->select_layer_opaque_pixels(id);
    }
  });
  layer_list->set_thumbnail_click_callback([this](QListWidgetItem* item, LayerCtrlClickTarget target,
                                                  Qt::KeyboardModifiers modifiers) {
    if (canvas_ == nullptr || item == nullptr) {
      return;
    }
    const auto id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    auto* layer = document().find_layer(id);
    if (layer == nullptr) {
      return;
    }
    if (target == LayerCtrlClickTarget::MaskThumbnail && (modifiers & Qt::ShiftModifier) != 0) {
      const auto disabled = std::as_const(*layer).mask().has_value() && std::as_const(*layer).mask()->disabled;
      document().set_active_layer(id);
      set_active_layer_mask_disabled(!disabled);
      return;
    }
    if (target == LayerCtrlClickTarget::VectorMaskThumbnail && (modifiers & Qt::ShiftModifier) != 0) {
      const auto* mask = std::as_const(*layer).vector_mask();
      if (mask != nullptr) {
        document().set_active_layer(id);
        set_active_layer_vector_mask_disabled(!mask->disabled);
      }
      return;
    }
    if (target == LayerCtrlClickTarget::SmartFilterMaskThumbnail &&
        (modifiers & Qt::ShiftModifier) != 0) {
      const auto* stack = std::as_const(*layer).smart_filter_stack();
      if (stack != nullptr) {
        document().set_active_layer(id);
        set_smart_filter_mask_enabled(id, !stack->mask.enabled);
      }
      return;
    }
    const auto was_active = document().active_layer_id().has_value() && *document().active_layer_id() == id;
    document().set_active_layer(id);
    restyle_layer_rows(layer_list_);
    if (target == LayerCtrlClickTarget::SmartFilterMaskThumbnail) {
      const auto editing_mask =
          was_active && canvas_->editing_smart_filter_mask() &&
          canvas_->smart_filter_mask_owner_id() == id;
      if ((modifiers & Qt::AltModifier) != 0) {
        const auto showing_mask =
            editing_mask && canvas_->mask_display_mode() ==
                                CanvasWidget::MaskDisplayMode::Grayscale;
        if (!editing_mask &&
            !set_smart_filter_mask_edit_target_ui(
                id, CanvasWidget::MaskDisplayMode::Grayscale, false)) {
          return;
        }
        canvas_->set_mask_display_mode(
            showing_mask ? CanvasWidget::MaskDisplayMode::None
                         : CanvasWidget::MaskDisplayMode::Grayscale);
        refresh_layer_controls();
        statusBar()->showMessage(resolve_modifier_names(
            showing_mask
                ? tr("Editing Smart Filter mask")
                : tr("Showing the Smart Filter mask. %ALT%-click the mask thumbnail to return.")));
        return;
      }
      if (editing_mask) {
        set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, true);
      } else {
        static_cast<void>(set_smart_filter_mask_edit_target_ui(
            id, CanvasWidget::MaskDisplayMode::Overlay, true));
      }
      return;
    }
    if (target == LayerCtrlClickTarget::MaskThumbnail && (modifiers & Qt::AltModifier) != 0) {
      const auto showing_mask =
          was_active && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask &&
          canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Grayscale;
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, false);
      canvas_->set_mask_display_mode(showing_mask ? CanvasWidget::MaskDisplayMode::None
                                                  : CanvasWidget::MaskDisplayMode::Grayscale);
      refresh_layer_controls();
      statusBar()->showMessage(resolve_modifier_names(
          showing_mask ? tr("Editing layer mask")
                       : tr("Showing the layer mask. %ALT%-click the mask thumbnail to return.")));
      return;
    }
    if (target == LayerCtrlClickTarget::VectorMaskThumbnail && (modifiers & Qt::AltModifier) != 0) {
      const auto showing_mask =
          was_active && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask &&
          canvas_->mask_display_mode() == CanvasWidget::MaskDisplayMode::Grayscale;
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::VectorMask, false);
      canvas_->set_mask_display_mode(showing_mask ? CanvasWidget::MaskDisplayMode::None
                                                  : CanvasWidget::MaskDisplayMode::Grayscale);
      refresh_layer_controls();
      statusBar()->showMessage(resolve_modifier_names(
          showing_mask ? tr("Editing vector mask")
                       : tr("Showing the vector mask. %ALT%-click the thumbnail to return.")));
      return;
    }
    if (target == LayerCtrlClickTarget::VectorMaskThumbnail) {
      const auto editing_vector_mask_already =
          was_active && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::VectorMask;
      set_layer_edit_target_ui(editing_vector_mask_already
                                   ? CanvasWidget::LayerEditTarget::Content
                                   : CanvasWidget::LayerEditTarget::VectorMask,
                               true);
      if (!editing_vector_mask_already) {
        statusBar()->showMessage(tr("Editing the vector mask path with the pen and path tools"));
      }
      return;
    }
    const auto editing_mask_already =
        was_active && canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::Mask;
    set_layer_edit_target_ui(target == LayerCtrlClickTarget::MaskThumbnail && !editing_mask_already
                                 ? CanvasWidget::LayerEditTarget::Mask
                                 : CanvasWidget::LayerEditTarget::Content,
                             true);
  });
  layer_list_ = layer_list;
  layer_list_->setObjectName(QStringLiteral("layerList"));
  layer_list_->setMinimumWidth(kLayerListMinimumWidth);
  layer_list_->setMinimumHeight(120);
  layer_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  layer_list_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  layer_list_->setTextElideMode(Qt::ElideNone);
  layer_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  layer_list_->setDragEnabled(true);
  layer_list_->setAcceptDrops(true);
  layer_list_->setDropIndicatorShown(true);
  layer_list_->setDragDropOverwriteMode(false);
  layer_list_->setDefaultDropAction(Qt::MoveAction);
  layer_list_->setDragDropMode(QAbstractItemView::InternalMove);
  layer_list_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(layer_list_, &QListWidget::itemSelectionChanged, this, [this] { set_active_layer_from_selection(); });
  layer_list->set_escape_callback([this] { deselect_all_layers(); });
  layer_list->set_item_double_click_callback([this](QListWidgetItem*) {
    auto& doc = document();
    const auto active = doc.active_layer_id();
    const auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
    // Groups fall through to the layer styles dialog like every other layer:
    // group layer effects render since July 2026 (Photoshop opens the same
    // dialog for a folder row double-click).
    if (layer != nullptr && layer->kind() == LayerKind::Adjustment) {
      edit_active_adjustment_layer();
      return;
    }
    // Smart objects and shape layers deliberately fall through to the layer
    // styles dialog too: their contents / appearance open via the row's
    // smart-object or vector badge button (or the menus), and layer styles
    // apply to shapes as well, so double-click stays consistent for every
    // layer (Seth, September 2026).
    edit_active_layer_style();
  });
  layer_list->set_inline_rename_callback(
      [this](LayerId id, const QString& name) { apply_layer_rename(id, name); });
  layer_list->set_content_thumbnail_double_click_callback([this](QListWidgetItem* item) {
    const auto layer_id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
    if (layer_id == 0) {
      return;
    }
    // Adjustment thumbnails are glyphs with no canvas extent; keep them
    // opening their settings like the rest of the row does.
    const auto* layer = std::as_const(document()).find_layer(layer_id);
    if (layer != nullptr && layer->kind() == LayerKind::Adjustment) {
      edit_active_adjustment_layer();
      return;
    }
    if (layer != nullptr && layer_is_text(*layer)) {
      // Editing can rebuild the rows. Leave the thumbnail's event first.
      const auto session_id = session().session_id;
      QTimer::singleShot(0, this, [this, layer_id, session_id] {
        if (has_active_document() && session().session_id == session_id) {
          edit_text_layer(layer_id);
        }
      });
      return;
    }
    zoom_canvas_to_layer_content(layer_id);
  });
  layer_list->set_smart_filter_double_click_callback(
      [this](QListWidgetItem* item, std::size_t execution_index) {
        const auto layer_id = static_cast<LayerId>(item->data(kLayerIdRole).toULongLong());
        if (layer_id == 0) {
          return;
        }
        // Deferred like the entry buttons: opening the dialog rebuilds the
        // layer list, which would delete the row mid-event otherwise.
        QTimer::singleShot(0, this, [this, layer_id, execution_index] {
          edit_smart_filter(layer_id, execution_index);
        });
      });
  connect(layer_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    set_layer_visibility_from_item(item);
  });
  connect(layer_list_, &QListWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_layer_context_menu(position); });
  connect(layer_list_->model(), &QAbstractItemModel::rowsMoved, this, [this] {
    if (updating_layer_list_) {
      return;
    }
    if (const auto* list = dynamic_cast<const LayerListWidget*>(layer_list_); list != nullptr && list->drop_in_progress()) {
      return;
    }
    QTimer::singleShot(0, this, [this] { reorder_layers_from_list(); });
  });

  // One compact row (Photoshop-style): the blend combo hugs its longest mode
  // name and Opacity/Fill are prefixed spin boxes with the toolbar popup
  // slider, so the panel spends one line here instead of three. When the
  // stack is narrower than that line (long translated prefixes make it wider
  // than the footer buttons), Opacity and Fill wrap onto a second line
  // instead of raising the dock's minimum width, and Fill onto a third in a
  // narrower column (GitHub issue 199). No extra side insets: the panel margin
  // alone places the row.
  auto* blend_opacity_row = new WrappingControlRowLayout(6);
  blend_combo_ = new QComboBox(layers_panel);
  add_blend_mode_items(blend_combo_);
  blend_combo_->setObjectName(QStringLiteral("layerBlendModeCombo"));
  blend_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  bind_tooltip(blend_combo_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Blend mode"));
  blend_opacity_row->addWidget(blend_combo_);
  connect(blend_combo_, &QComboBox::currentIndexChanged, this, [this](int index) { set_active_layer_blend(index); });
  register_document_widget(blend_combo_);

  opacity_spin_ = new QSpinBox(layers_panel);
  opacity_spin_->setObjectName(QStringLiteral("layerOpacitySpin"));
  opacity_spin_->setRange(0, 100);
  opacity_spin_->setValue(100);
  opacity_spin_->setPrefix(tr("Opacity: "));
  opacity_spin_->setSuffix(percent_suffix());
  configure_toolbar_spinbox(opacity_spin_, 52);
  install_prefix_scrub(opacity_spin_);  // drag "Opacity:" to scrub (GitHub issue 46)
  blend_opacity_row->addWidget(opacity_spin_);
  connect(opacity_spin_, &QSpinBox::valueChanged, this, [this](int value) { set_active_layer_opacity(value); });
  connect(opacity_spin_, &QSpinBox::editingFinished, this, [this] { finish_pending_layer_opacity_edit(); });
  register_document_widget(opacity_spin_);

  fill_opacity_spin_ = new QSpinBox(layers_panel);
  fill_opacity_spin_->setObjectName(QStringLiteral("layerFillOpacitySpin"));
  fill_opacity_spin_->setRange(0, 100);
  fill_opacity_spin_->setValue(100);
  fill_opacity_spin_->setPrefix(tr("Fill: "));
  fill_opacity_spin_->setSuffix(percent_suffix());
  configure_toolbar_spinbox(fill_opacity_spin_, 52);
  install_prefix_scrub(fill_opacity_spin_);
  blend_opacity_row->addWidget(fill_opacity_spin_);
  connect(fill_opacity_spin_, &QSpinBox::valueChanged, this,
          [this](int value) { set_active_layer_fill_opacity(value); });
  connect(fill_opacity_spin_, &QSpinBox::editingFinished, this,
          [this] { finish_pending_layer_fill_opacity_edit(); });
  register_document_widget(fill_opacity_spin_);
  register_retranslation([this] {
    if (opacity_spin_ == nullptr || fill_opacity_spin_ == nullptr) {
      return;
    }
    opacity_spin_->setPrefix(tr("Opacity: "));
    fill_opacity_spin_->setPrefix(tr("Fill: "));
    // Re-measures the fixed width for the translated prefixes; the popup
    // install inside is a guarded no-op on repeat calls.
    configure_toolbar_spinbox(opacity_spin_, 52);
    configure_toolbar_spinbox(fill_opacity_spin_, 52);
    // Deferred so the blend combo's own retranslation (which can change its
    // minimum width) lands before the stack minimum is re-measured.
    QTimer::singleShot(0, this, [this] { update_right_dock_minimum_width(); });
  });
  layers_layout->addLayout(blend_opacity_row);

  // Left-packed row: the label, buttons, and filter hug the left edge with the
  // filter absorbing the leftover width (a grid here split the extra space
  // across its columns and stranded the buttons mid-panel). Flush with the
  // panel margin like the blend row, so the filter's right edge tracks the
  // layer list's. In a narrow column the filter wraps onto a line of its own
  // (GitHub issue 199).
  auto* lock_row = new WrappingControlRowLayout(6);
  auto* lock_group = new QHBoxLayout();
  lock_group->setContentsMargins(0, 0, 0, 0);
  lock_group->setSpacing(6);
  auto* lock_label = new QLabel(tr("Lock"), layers_panel);
  bind_widget_text(lock_label, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Lock"));
  lock_group->addWidget(lock_label);
  auto* lock_controls = new QHBoxLayout();
  lock_controls->setContentsMargins(0, 0, 0, 0);
  lock_controls->setSpacing(4);
  const auto make_lock_button = [this, layers_panel](const QString& object_name, const QString& icon_key,
                                                     const QString& tooltip, LayerLockFlags flag) {
    auto* button = new QToolButton(layers_panel);
    button->setObjectName(object_name);
    button->setProperty("layerLockControl", true);
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIcon(simple_icon(icon_key, QColor(226, 232, 240)));
    button->setIconSize(QSize(15, 15));
    button->setToolTip(tooltip);
    button->setFixedSize(24, 24);
    connect(button, &QToolButton::toggled, this, [this, flag](bool checked) {
      set_active_layer_lock_flag(flag, checked);
    });
    register_document_widget(button);
    return button;
  };
  lock_transparent_pixels_button_ =
      make_lock_button(QStringLiteral("layerLockTransparentButton"), QStringLiteral("AL"),
                       tr("Lock transparent pixels"), kLayerLockTransparentPixels);
  lock_image_pixels_button_ =
      make_lock_button(QStringLiteral("layerLockPixelsButton"), QStringLiteral("fill"),
                       tr("Lock image pixels"), kLayerLockImagePixels);
  lock_position_button_ =
      make_lock_button(QStringLiteral("layerLockPositionButton"), QStringLiteral("TR"),
                       tr("Lock position"), kLayerLockPosition);
  lock_all_button_ = new QToolButton(layers_panel);
  lock_all_button_->setObjectName(QStringLiteral("layerLockAllButton"));
  lock_all_button_->setProperty("layerLockControl", true);
  lock_all_button_->setCheckable(true);
  lock_all_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  lock_all_button_->setIcon(simple_icon(QStringLiteral("lock")));
  lock_all_button_->setIconSize(QSize(15, 15));
  bind_tooltip(lock_all_button_, QT_TR_NOOP("Lock all"));
  lock_all_button_->setFixedSize(24, 24);
  connect(lock_all_button_, &QToolButton::toggled, this, [this](bool checked) { set_active_layer_lock_all(checked); });
  register_document_widget(lock_all_button_);
  lock_controls->addWidget(lock_transparent_pixels_button_);
  lock_controls->addWidget(lock_image_pixels_button_);
  lock_controls->addWidget(lock_position_button_);
  lock_controls->addWidget(lock_all_button_);
  layer_name_filter_edit_ = new QLineEdit(layers_panel);
  layer_name_filter_edit_->setObjectName(QStringLiteral("layerNameFilterEdit"));
  layer_name_filter_edit_->setClearButtonEnabled(true);
  layer_name_filter_edit_->setFixedHeight(24);
  bind_widget_text(layer_name_filter_edit_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Filter layers by name..."));
  connect(layer_name_filter_edit_, &QLineEdit::textChanged, this, [this] { refresh_layer_list(); });
  register_document_widget(layer_name_filter_edit_);
  lock_group->addLayout(lock_controls);
  lock_row->add_layout(lock_group);
  // Rides the otherwise-empty stretch space beside the lock buttons instead of
  // spending a panel row of its own, until that space drops under a few
  // characters' typing room.
  lock_row->addWidget(layer_name_filter_edit_);
  lock_row->set_stretch_widget(layer_name_filter_edit_, kLayerNameFilterMinimumWidth);
  layers_layout->addLayout(lock_row);
  layers_layout->addWidget(layer_list_, 1);

  // One row of full-width buttons at 5 px spacing is the row the column opens
  // to fit. A narrower column narrows them at 3 px spacing, then wraps them four
  // over four (GitHub issue 199).
  auto* layer_buttons = new WrappingButtonRowLayout(5, 3);
  auto* add_button = new QPushButton(layers_panel);
  auto* add_folder_button = new QPushButton(layers_panel);
  auto* adjustment_button = new QToolButton(layers_panel);
  layer_styles_button_ = new QToolButton(layers_panel);
  auto* styles_button = layer_styles_button_;
  add_layer_mask_button_ = new QPushButton(layers_panel);
  auto* add_mask_button = add_layer_mask_button_;
  auto* duplicate_button = new QPushButton(layers_panel);
  auto* animation_button = new QPushButton(layers_panel);
  auto* delete_button = new QPushButton(layers_panel);
  add_button->setObjectName(QStringLiteral("layerNewButton"));
  adjustment_button->setObjectName(QStringLiteral("layerNewAdjustmentButton"));
  styles_button->setObjectName(QStringLiteral("layerStylesButton"));
  add_folder_button->setObjectName(QStringLiteral("layerNewFolderButton"));
  add_mask_button->setObjectName(QStringLiteral("layerAddMaskButton"));
  duplicate_button->setObjectName(QStringLiteral("layerDuplicateButton"));
  animation_button->setObjectName(QStringLiteral("layerAnimationButton"));
  delete_button->setObjectName(QStringLiteral("layerDeleteButton"));
  add_button->setIcon(simple_icon(QStringLiteral("new")));
  add_folder_button->setIcon(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)));
  adjustment_button->setIcon(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)));
  // The same fx art as the layer-row badge, so the button and the badge read as one feature.
  styles_button->setIcon(simple_icon(QStringLiteral("badge-fx")));
  add_mask_button->setIcon(simple_icon(QStringLiteral("mask"), QColor(210, 220, 230)));
  duplicate_button->setIcon(simple_icon(QStringLiteral("dup")));
  animation_button->setIcon(simple_icon(QStringLiteral("film")));
  delete_button->setIcon(simple_icon(QStringLiteral("trash")));
  bind_tooltip(add_button, QT_TR_NOOP("New Layer"));
  bind_tooltip(add_folder_button, QT_TR_NOOP("New Folder"));
  bind_tooltip(adjustment_button, QT_TR_NOOP("New Fill or Adjustment Layer"));
  bind_tooltip(styles_button, QT_TR_NOOP("Layer Styles"));
  bind_tooltip(add_mask_button, QT_TR_NOOP("Add Layer Mask"));
  bind_tooltip(duplicate_button, QT_TR_NOOP("Duplicate Layer"));
  bind_tooltip(animation_button, QT_TR_NOOP("Animation Preview"));
  bind_tooltip(delete_button, QT_TR_NOOP("Delete Layer"));
  for (auto* button :
       {add_button, add_folder_button, add_mask_button, duplicate_button, animation_button, delete_button}) {
    button->setProperty("layerActionButton", true);
    button->setIconSize(QSize(24, 24));
    button->setFixedSize(38, 34);
    // Panel buttons never take keyboard focus (the Channels/Paths panel rule):
    // a focused button would swallow canvas keys after a click.
    button->setFocusPolicy(Qt::NoFocus);
  }
  add_button->setProperty("layerDropAction", QStringLiteral("duplicate"));
  add_folder_button->setProperty("layerDropAction", QStringLiteral("folder"));
  duplicate_button->setProperty("layerDropAction", QStringLiteral("duplicate"));
  delete_button->setProperty("layerDropAction", QStringLiteral("delete"));
  for (auto* button : {add_button, add_folder_button, duplicate_button, delete_button}) {
    button->setAcceptDrops(true);
    button->installEventFilter(this);
  }
  for (auto* button : {adjustment_button, styles_button}) {
    button->setProperty("layerActionButton", true);
    button->setIconSize(QSize(24, 24));
    button->setFixedSize(38, 34);
    button->setFocusPolicy(Qt::NoFocus);
    button->setPopupMode(QToolButton::InstantPopup);
  }
  auto* adjustment_button_menu = new QMenu(adjustment_button);
  adjustment_button_menu->setObjectName(QStringLiteral("layerNewAdjustmentButtonMenu"));
  // The fill layers first, then the adjustments, built by the same functions as Layer > New
  // Fill Layer and Layer > New Adjustment Layer, so a new kind lands in both (GitHub issue 252).
  populate_new_fill_layer_menu(adjustment_button_menu);
  adjustment_button_menu->addSeparator();
  populate_new_adjustment_layer_menu(adjustment_button_menu);
  hide_menu_action_icons(adjustment_button_menu);
  adjustment_button->setMenu(adjustment_button_menu);
  // GitHub issue 94: the effects are reachable from the footer, not only from a
  // row's double-click or context menu. Every entry opens the one Layer Style
  // dialog; an effect entry opens it on that effect, turned on.
  auto* styles_button_menu = new QMenu(styles_button);
  styles_button_menu->setObjectName(QStringLiteral("layerStylesButtonMenu"));
  if (layer_blending_options_action_ != nullptr) {
    styles_button_menu->addAction(layer_blending_options_action_);
    styles_button_menu->addSeparator();
  }
  const auto add_effect_entry = [&](const char* text, const char* object_name, LayerStyleEffect effect) {
    auto* action = styles_button_menu->addAction(QString());
    action->setObjectName(QLatin1String(object_name));
    bind_action_text(action, text);
    connect(action, &QAction::triggered, this, [this, effect] { edit_active_layer_style(effect); });
  };
  add_effect_entry(QT_TR_NOOP("Bevel && Emboss..."), "layerStylesBevelEmbossAction", LayerStyleEffect::BevelEmboss);
  add_effect_entry(QT_TR_NOOP("Stroke..."), "layerStylesStrokeAction", LayerStyleEffect::Stroke);
  add_effect_entry(QT_TR_NOOP("Inner Shadow..."), "layerStylesInnerShadowAction", LayerStyleEffect::InnerShadow);
  add_effect_entry(QT_TR_NOOP("Inner Glow..."), "layerStylesInnerGlowAction", LayerStyleEffect::InnerGlow);
  add_effect_entry(QT_TR_NOOP("Satin..."), "layerStylesSatinAction", LayerStyleEffect::Satin);
  add_effect_entry(QT_TR_NOOP("Color Overlay..."), "layerStylesColorOverlayAction", LayerStyleEffect::ColorOverlay);
  add_effect_entry(QT_TR_NOOP("Gradient Overlay..."), "layerStylesGradientOverlayAction",
                   LayerStyleEffect::GradientOverlay);
  add_effect_entry(QT_TR_NOOP("Pattern Overlay..."), "layerStylesPatternOverlayAction",
                   LayerStyleEffect::PatternOverlay);
  add_effect_entry(QT_TR_NOOP("Outer Glow..."), "layerStylesOuterGlowAction", LayerStyleEffect::OuterGlow);
  add_effect_entry(QT_TR_NOOP("Drop Shadow..."), "layerStylesDropShadowAction", LayerStyleEffect::DropShadow);
  // The shared Edit Layer Styles... action is also gated by the layer context
  // menu; give it this button's state while this menu is open.
  connect(styles_button_menu, &QMenu::aboutToShow, this, [this] {
    if (layer_blending_options_action_ != nullptr) {
      layer_blending_options_action_->setEnabled(can_edit_layer_styles());
    }
  });
  styles_button->setMenu(styles_button_menu);
  layer_buttons->addWidget(add_button);
  layer_buttons->addWidget(add_folder_button);
  layer_buttons->addWidget(adjustment_button);
  layer_buttons->addWidget(styles_button);
  layer_buttons->addWidget(add_mask_button);
  layer_buttons->addWidget(duplicate_button);
  layer_buttons->addWidget(animation_button);
  layer_buttons->addWidget(delete_button);
  layers_layout->addLayout(layer_buttons);
  connect(add_button, &QPushButton::clicked, this, [this] { add_layer(); });
  connect(add_folder_button, &QPushButton::clicked, this, [this] { create_layer_folder(); });
  connect(add_mask_button, &QPushButton::clicked, this, [this] { add_layer_mask(); });
  connect(duplicate_button, &QPushButton::clicked, this, [this] { duplicate_active_layer(); });
  connect(animation_button, &QPushButton::clicked, this, [this] { toggle_animation_preview_window(); });
  connect(delete_button, &QPushButton::clicked, this, [this] { delete_active_layer(); });
  for (auto* widget : {static_cast<QWidget*>(add_button), static_cast<QWidget*>(add_folder_button),
                       static_cast<QWidget*>(adjustment_button), static_cast<QWidget*>(duplicate_button),
                       static_cast<QWidget*>(animation_button), static_cast<QWidget*>(delete_button)}) {
    register_document_widget(widget);
  }
  // Not document widgets: their enabled state also depends on the layer
  // selection, so refresh_add_layer_mask_button_state and
  // refresh_layer_styles_button_state own it.
  add_mask_button->setEnabled(false);
  styles_button->setEnabled(false);

  tab_specs.push_back(PanelSpec{.id = QStringLiteral("layersDock"),
                                .object_prefix = QStringLiteral("layers"),
                                .title = tr("Layers"),
                                .content = layers_panel,
                                .expanded_minimum_height = 300});

  channel_panel_ = new ChannelPanel(this);
  tab_specs.push_back(PanelSpec{.id = QStringLiteral("channelsDock"),
                                .object_prefix = QStringLiteral("channels"),
                                .title = tr("Channels"),
                                .content = channel_panel_,
                                .expanded_minimum_height = 190});

  const auto make_channel_action = [this](QAction*& target, const char* text, const char* object_name,
                                          const char* hotkey_id, auto callback) {
    target = new QAction(tr(text), this);
    target->setObjectName(QLatin1String(object_name));
    bind_action_text(target, text);
    register_hotkey(target, QLatin1String(hotkey_id), QKeySequence(), QStringLiteral("channels"));
    register_document_action(target);
    connect(target, &QAction::triggered, this, callback);
  };
  make_channel_action(channel_new_action_, QT_TR_NOOP("New Channel"), "channelNewAction", "channel.new",
                      [this] { create_alpha_channel(); });
  make_channel_action(channel_save_selection_action_, QT_TR_NOOP("Save Selection as Channel"),
                      "channelSaveSelectionAction", "channel.save_selection",
                      [this] { save_selection_as_channel(); });
  make_channel_action(channel_load_selection_action_, QT_TR_NOOP("Load Channel as Selection"),
                      "channelLoadSelectionAction", "channel.load_selection",
                      [this] { load_channel_as_selection(); });
  make_channel_action(channel_rename_action_, QT_TR_NOOP("Rename Channel"), "channelRenameAction", "channel.rename",
                      [this] { rename_active_channel(); });
  make_channel_action(channel_invert_action_, QT_TR_NOOP("Invert Channel"), "channelInvertAction", "channel.invert",
                      [this] { invert_active_channel(); });
  make_channel_action(channel_delete_action_, QT_TR_NOOP("Delete Channel"), "channelDeleteAction", "channel.delete",
                      [this] { delete_active_channel(); });
  make_channel_action(channel_new_layer_action_, QT_TR_NOOP("New Layer from Channel"), "channelNewLayerAction",
                      "channel.new_layer", [this] { new_layer_from_channel(false); });
  make_channel_action(channel_new_color_layer_action_, QT_TR_NOOP("New Layer from Channel Color"),
                      "channelNewColorLayerAction", "channel.new_color_layer", [this] { new_layer_from_channel(true); });
  channel_new_action_->setIcon(simple_icon(QStringLiteral("new")));
  channel_save_selection_action_->setIcon(simple_icon(QStringLiteral("channel-save-selection")));
  channel_load_selection_action_->setIcon(simple_icon(QStringLiteral("channel-load-selection")));
  channel_rename_action_->setIcon(simple_icon(QStringLiteral("RN")));
  channel_invert_action_->setIcon(simple_icon(QStringLiteral("inv")));
  channel_delete_action_->setIcon(simple_icon(QStringLiteral("trash")));
  channel_panel_->set_actions(channel_new_action_, channel_save_selection_action_,
                              channel_load_selection_action_, channel_rename_action_, channel_invert_action_,
                              channel_delete_action_);
  channel_panel_->set_layer_actions(channel_new_layer_action_, channel_new_color_layer_action_);
  channel_panel_->set_target_callback([this](ChannelPanel::RowKind kind, ChannelId id, bool overlay) {
    set_channel_edit_target(kind, id, overlay);
  });
  channel_panel_->set_load_selection_callback(
      [this](ChannelPanel::RowKind kind, ChannelId id) { load_channel_as_selection(kind, id); });
  channel_panel_->set_reorder_callback(
      [this](std::vector<ChannelId> order) { reorder_channels_from_panel(std::move(order)); });
  register_document_widget(channel_panel_);
  refresh_channel_panel();

  paths_panel_ = new PathsPanel(this);
  tab_specs.push_back(PanelSpec{.id = QStringLiteral("pathsDock"),
                                .object_prefix = QStringLiteral("paths"),
                                .title = tr("Paths"),
                                .content = paths_panel_,
                                .expanded_minimum_height = 190});

  const auto make_path_action = [this](QAction*& target, const char* text, const char* object_name,
                                       const char* hotkey_id, auto callback) {
    target = new QAction(tr(text), this);
    target->setObjectName(QLatin1String(object_name));
    bind_action_text(target, text);
    register_hotkey(target, QLatin1String(hotkey_id), QKeySequence(), QStringLiteral("paths"));
    register_document_action(target);
    connect(target, &QAction::triggered, this, callback);
  };
  make_path_action(path_new_action_, QT_TR_NOOP("New Path"), "pathNewAction", "path.new",
                   [this] { new_saved_path(); });
  make_path_action(path_fill_action_, QT_TR_NOOP("Fill Path"), "pathFillAction", "path.fill",
                   [this] { fill_active_path(); });
  make_path_action(path_stroke_action_, QT_TR_NOOP("Stroke Path"), "pathStrokeAction", "path.stroke",
                   [this] { stroke_active_path(); });
  make_path_action(path_make_selection_action_, QT_TR_NOOP("Make Selection"),
                   "pathMakeSelectionAction", "path.make_selection",
                   [this] { make_selection_from_path(); });
  make_path_action(path_from_selection_action_, QT_TR_NOOP("Make Work Path from Selection"),
                   "pathFromSelectionAction", "path.from_selection",
                   [this] { make_work_path_from_selection(); });
  make_path_action(path_duplicate_action_, QT_TR_NOOP("Duplicate Path"), "pathDuplicateAction",
                   "path.duplicate", [this] { duplicate_selected_path(); });
  make_path_action(path_clipping_action_, QT_TR_NOOP("Clipping Path"), "pathClippingAction",
                   "path.clipping", [this] { toggle_selected_path_clipping(); });
  path_clipping_action_->setCheckable(true);
  make_path_action(path_delete_action_, QT_TR_NOOP("Delete Path"), "pathDeleteAction", "path.delete",
                   [this] { delete_selected_path(); });
  path_new_action_->setIcon(simple_icon(QStringLiteral("new")));
  path_fill_action_->setIcon(simple_icon(QStringLiteral("fill")));
  path_stroke_action_->setIcon(simple_icon(QStringLiteral("stroke-path")));
  path_make_selection_action_->setIcon(simple_icon(QStringLiteral("channel-load-selection")));
  path_from_selection_action_->setIcon(simple_icon(QStringLiteral("channel-save-selection")));
  path_duplicate_action_->setIcon(simple_icon(QStringLiteral("dup")));
  path_delete_action_->setIcon(simple_icon(QStringLiteral("trash")));
  paths_panel_->set_actions(path_new_action_, path_fill_action_, path_stroke_action_,
                            path_make_selection_action_, path_from_selection_action_,
                            path_duplicate_action_, path_clipping_action_, path_delete_action_,
                            path_simplify_action_);
  paths_panel_->set_target_callback([this](PathsPanel::RowKind kind, DocumentPathId id) {
    handle_paths_panel_target(static_cast<int>(kind), id);
  });
  paths_panel_->set_deselect_callback([this] { handle_paths_panel_deselect(); });
  paths_panel_->set_rename_callback(
      [this](DocumentPathId id, QString name) { rename_document_path(id, name); });
  paths_panel_->set_save_work_path_callback([this] { save_work_path_as_named(); });
  paths_panel_->set_load_selection_callback([this](PathsPanel::RowKind kind, DocumentPathId id) {
    load_path_as_selection(static_cast<int>(kind), id);
  });
  paths_panel_->set_reorder_callback(
      [this](std::vector<DocumentPathId> order) { reorder_paths_from_panel(std::move(order)); });
  register_document_widget(paths_panel_);
  refresh_paths_panel();

  history_list_ = new QListWidget(this);
  history_list_->setObjectName(QStringLiteral("historyList"));
  // itemClicked (not currentRowChanged): the rebuild's programmatic
  // setCurrentRow must never trigger a jump, and keyboard focus moves stay
  // visual-only until the next rebuild re-asserts the current state.
  connect(history_list_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem* item) { handle_history_row_clicked(item); });
  history_list_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(history_list_, &QListWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_history_context_menu(position); });
  // Disabled while no document is open or a preview dialog holds the edit lock,
  // matching the undo/redo actions.
  register_document_widget(history_list_);
  auto* history_panel = new QWidget(this);
  history_panel->setObjectName(QStringLiteral("historyPanel"));
  auto* history_layout = new QVBoxLayout(history_panel);
  history_layout->setContentsMargins(0, 0, 0, 0);
  history_layout->setSpacing(0);
  history_layout->addWidget(history_list_);
  panel_specs.push_back(PanelSpec{.id = QStringLiteral("historyDock"),
                                  .object_prefix = QStringLiteral("history"),
                                  .title = tr("History"),
                                  .content = history_panel,
                                  .expanded_minimum_height = kHistoryDockExpandedMinimumHeight,
                                  .preferred_height = kHistoryDockPreferredHeight});

  auto* properties_scroll = new QScrollArea(this);
  properties_scroll->setObjectName(QStringLiteral("propertiesScrollArea"));
  properties_scroll->setFrameShape(QFrame::NoFrame);
  properties_scroll->setWidgetResizable(true);
  // Scrolls sideways once the column is dragged narrower than the panel.
  properties_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  auto* properties_panel = new QWidget(properties_scroll);
  properties_panel->setObjectName(QStringLiteral("propertiesPanel"));
  auto* properties_layout = new QVBoxLayout(properties_panel);
  properties_layout->setContentsMargins(6, 6, 6, 6);
  properties_layout->setSpacing(4);
  const auto add_properties_label = [properties_panel, properties_layout](const QString& object_name) {
    auto* label = new QLabel(properties_panel);
    label->setObjectName(object_name);
    label->setWordWrap(false);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    label->hide();
    properties_layout->addWidget(label);
    return label;
  };
  document_info_label_ = new QLabel(properties_panel);
  document_info_label_->setObjectName(QStringLiteral("documentInfoLabel"));
  document_info_label_->setWordWrap(false);
  document_info_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  document_info_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  properties_layout->addWidget(document_info_label_);
  active_layer_info_label_ = add_properties_label(QStringLiteral("activeLayerInfoLabel"));
  active_layer_geometry_label_ = add_properties_label(QStringLiteral("activeLayerGeometryLabel"));
  active_layer_mask_label_ = add_properties_label(QStringLiteral("activeLayerMaskLabel"));
  active_layer_adjustment_label_ = add_properties_label(QStringLiteral("activeLayerAdjustmentLabel"));
  active_layer_text_label_ = add_properties_label(QStringLiteral("activeLayerTextLabel"));
  active_layer_shape_label_ = add_properties_label(QStringLiteral("activeLayerShapeLabel"));

  // W / H of the active shape layer, one compact row like the options bar's
  // readouts (which share the same apply path and stay in sync with these).
  properties_shape_size_panel_ = new QWidget(properties_panel);
  properties_shape_size_panel_->setObjectName(QStringLiteral("propertiesShapeSizePanel"));
  properties_shape_size_panel_->hide();
  auto* properties_shape_size_row = new QHBoxLayout(properties_shape_size_panel_);
  properties_shape_size_row->setContentsMargins(0, 2, 0, 2);
  properties_shape_size_row->setSpacing(4);
  const auto add_shape_size_label = [this, properties_shape_size_row](const char* source) {
    auto* label = new QLabel(properties_shape_size_panel_);
    bind_widget_text(label, source);
    properties_shape_size_row->addWidget(label);
    return label;
  };
  const auto make_properties_shape_size_spin = [this, properties_shape_size_row](const char* name,
                                                                                 const char* tooltip) {
    auto* spin = new UnitSpinBox(SpinUnit::Pixels, properties_shape_size_panel_);
    spin->setObjectName(QLatin1String(name));
    spin->setRange(0.1, 60000.0);
    spin->setDecimals(1);
    spin->setSingleStep(1.0);
    spin->setKeyboardTracking(false);
    spin->setEnabled(false);
    bind_tooltip(spin, tooltip);
    configure_dialog_spinbox(spin, 96);
    spin->setFixedWidth(96);
    properties_shape_size_row->addWidget(spin);
    return spin;
  };
  add_shape_size_label(QT_TR_NOOP("W:"));
  properties_shape_width_spin_ =
      make_properties_shape_size_spin("propertiesShapeWidthSpin", QT_TR_NOOP("Width of the active shape"));
  properties_shape_link_size_button_ = new QPushButton(properties_shape_size_panel_);
  properties_shape_link_size_button_->setObjectName(QStringLiteral("propertiesShapeLinkSizeButton"));
  properties_shape_link_size_button_->setCheckable(true);
  properties_shape_link_size_button_->setIcon(simple_icon(QStringLiteral("link")));
  properties_shape_link_size_button_->setFixedWidth(28);
  bind_tooltip(properties_shape_link_size_button_,
               QT_TR_NOOP("Keep the shape's width and height in proportion"));
  properties_shape_link_size_button_->setEnabled(false);
  properties_shape_size_row->addWidget(properties_shape_link_size_button_);
  add_shape_size_label(QT_TR_NOOP("H:"));
  properties_shape_height_spin_ =
      make_properties_shape_size_spin("propertiesShapeHeightSpin", QT_TR_NOOP("Height of the active shape"));
  properties_shape_size_row->addStretch(1);
  properties_layout->addWidget(properties_shape_size_panel_);
  install_scrub_labels_in(properties_shape_size_panel_);  // drag "W:" / "H:" to scrub (issue 46)
  // Photoshop's Properties panel shows shape W/H in the ruler unit.
  properties_shape_width_spin_->set_context_provider(document_unit_context_provider(true));
  properties_shape_height_spin_->set_context_provider(document_unit_context_provider(false));
  register_ruler_unit_field(properties_shape_width_spin_);
  register_ruler_unit_field(properties_shape_height_spin_);
  connect(properties_shape_width_spin_, &QDoubleSpinBox::valueChanged, this,
          [this](double value) { handle_vector_shape_size_value_changed(true, value); });
  connect(properties_shape_height_spin_, &QDoubleSpinBox::valueChanged, this,
          [this](double value) { handle_vector_shape_size_value_changed(false, value); });
  connect(properties_shape_link_size_button_, &QPushButton::toggled, this, [this](bool checked) {
    if (vector_shape_link_size_button_ != nullptr) {
      const QSignalBlocker blocker(vector_shape_link_size_button_);
      vector_shape_link_size_button_->setChecked(checked);
    }
  });

  build_properties_artboard_panel(properties_panel, properties_layout);

  properties_edit_appearance_button_ = new QPushButton(tr("Edit Appearance..."), properties_panel);
  properties_edit_appearance_button_->setObjectName(QStringLiteral("propertiesEditAppearanceButton"));
  bind_widget_text(properties_edit_appearance_button_, QT_TR_NOOP("Edit Appearance..."));
  properties_edit_appearance_button_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
  properties_edit_appearance_button_->hide();
  properties_layout->addWidget(properties_edit_appearance_button_);
  connect(properties_edit_appearance_button_, &QPushButton::clicked, this,
          [this] { edit_active_shape_appearance(); });
  active_tool_info_label_ = add_properties_label(QStringLiteral("activeToolInfoLabel"));
  properties_layout->addStretch(0);
  properties_scroll->setWidget(properties_panel);
  panel_specs.push_back(PanelSpec{.id = QStringLiteral("propertiesDock"),
                                  .object_prefix = QStringLiteral("properties"),
                                  .title = tr("Properties"),
                                  .content = properties_scroll,
                                  .expanded_maximum_height = kPropertiesDockMaximumHeight,
                                  .preferred_height = kPropertiesDockMaximumHeight});

  auto* info_panel = new QWidget(this);
  info_panel->setObjectName(QStringLiteral("infoPanel"));
  auto* info_layout = new QVBoxLayout(info_panel);
  info_layout->setContentsMargins(8, 8, 8, 8);
  canvas_info_label_ = new QLabel(info_panel);
  canvas_info_label_->setObjectName(QStringLiteral("canvasInfoLabel"));
  canvas_info_label_->setText(tr("X: -\nY: -\nRGB: -\nRect: -"));
  canvas_info_label_->setWordWrap(true);
  canvas_info_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  info_layout->addWidget(canvas_info_label_);
  info_layout->addStretch(1);
  panel_specs.push_back(PanelSpec{.id = QStringLiteral("infoDock"),
                                  .object_prefix = QStringLiteral("info"),
                                  .title = tr("Info"),
                                  .content = info_panel});

  create_palette_dock(panel_specs);

  // The column: the tab group first (it takes the spare height), then the rest in
  // order. History, Properties, Info and Palette start collapsed.
  panel_column_ = new PanelColumn(workspace_splitter_);
  panel_column_->setMinimumWidth(kRightDockFloorWidth);
  panel_column_->add_tab_group(QStringLiteral("panelTabGroup"), tab_specs, true);
  for (const auto& spec : panel_specs) {
    panel_column_->add_panel(spec, false);
  }
  // Each title shares its panel's translation; the tab texts follow the titles.
  static constexpr std::pair<const char*, const char*> kPanelTitles[] = {
      {"layersDock", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Layers")},
      {"channelsDock", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Channels")},
      {"pathsDock", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Paths")},
      {"historyDock", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "History")},
      {"propertiesDock", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Properties")},
      {"infoDock", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Info")},
      {"paletteDock", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Palette")},
  };
  for (const auto& [id, title] : kPanelTitles) {
    bind_widget_text(panel_column_->title_label(QLatin1String(id)), title);
  }
  panel_column_->refresh_tab_titles();
  register_retranslation([this] { panel_column_->refresh_tab_titles(); });
  connect(panel_column_, &PanelColumn::header_context_menu_requested, this,
          &MainWindow::show_panel_header_menu);
  // An expand or a reopened section raises the column's minimum: keep the window on
  // its screen (GitHub issue 108).
  const auto column_changed = [this] {
    fit_right_dock_floors_to_screen();
#ifdef Q_OS_WASM
    // The wasm backing store keeps stale pixels where a relayout moved the panels.
    update();
#endif
  };
  connect(panel_column_, &PanelColumn::panel_expanded_changed, this, column_changed);
  connect(panel_column_, &PanelColumn::panel_visibility_changed, this, column_changed);
  panel_column_->installEventFilter(this);

  // Beside the document tabs: the tabs take every width change, the column keeps its
  // width, and the splitter handle is the column's draggable edge.
  workspace_splitter_->addWidget(panel_column_);
  workspace_splitter_->setStretchFactor(0, 1);
  workspace_splitter_->setStretchFactor(1, 0);
  workspace_splitter_->setCollapsible(0, false);
  workspace_splitter_->setCollapsible(1, false);
  workspace_splitter_->handle(1)->setObjectName(QStringLiteral("rightDockResizeHandle"));
  connect(workspace_splitter_, &QSplitter::splitterMoved, this, [this] { handle_right_column_moved(); });
  update_right_dock_minimum_width();
  // Re-measure once the first event-loop pass has shown and styled the column: only
  // then can the real chrome be read.
  QTimer::singleShot(0, this, [this] { update_right_dock_minimum_width(); });
}

void MainWindow::create_palette_dock(std::vector<PanelSpec>& specs) {
  palette_panel_ = new PalettePanel(this);
  connect(palette_panel_, &PalettePanel::entry_clicked, this, [this](int index) {
    if (canvas_ == nullptr) {
      return;
    }
    const auto colors = displayed_palette_colors();
    if (index < 0 || index >= static_cast<int>(colors.size())) {
      return;
    }
    const QColor color(colors[static_cast<std::size_t>(index)].red, colors[static_cast<std::size_t>(index)].green,
                       colors[static_cast<std::size_t>(index)].blue);
    canvas_->set_primary_color(color);
    apply_primary_color_to_active_text_editor(color);
    // A swatch click also lands in any open color picker: the transient request
    // picker (layer-style colors, gradient stops, ...) takes it live through its
    // callback, and the persistent Foreground/Text color panel mirrors the new
    // state (blocked: set_primary_color above already applied it).
    const bool request_picker_took_color = apply_color_to_open_color_picker(color);
    bool text_color_panel_open = false;
    if (color_dialog_ != nullptr) {
      const auto target = color_dialog_->property("patchy.colorTarget").toString();
      text_color_panel_open = target == QStringLiteral("text");
      if (target == QStringLiteral("foreground") || text_color_panel_open) {
        if (auto* picker = color_dialog_->findChild<PatchyColorPicker*>(
                QStringLiteral("patchyAdvancedColorPicker"))) {
          const QSignalBlocker blocker(picker);
          picker->setCurrentColor(color);
        }
      }
    }
    // A swatch click also recolors what the active tool's options-bar color box edits
    // (issue 61), unless a request picker took the color for its own target: the selected
    // text layers with no session open (the Type tool, or the Text Color panel whose
    // blocked mirror above skips its own callback), and the solid shape paint plus the
    // selected shape layers while the shape appearance controls are live.
    if (!request_picker_took_color) {
      if ((current_tool_ == CanvasTool::Text || text_color_panel_open) &&
          canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr) {
        apply_text_color_to_selected_layers_debounced(color);
      }
      apply_swatch_color_to_shape_paint(color);
    }
    refresh_color_buttons();
    refresh_palette_panel();
    statusBar()->showMessage(tr("Foreground: palette index %1 (%2)").arg(index).arg(color.name()));
  });
  connect(palette_panel_, &PalettePanel::entry_edit_requested, this, [this](int index) { edit_palette_entry(index); });
  connect(palette_panel_, &PalettePanel::entry_name_requested, this, [this](int index) { rename_palette_entry(index); });
  connect(palette_panel_, &PalettePanel::entry_swap_requested, this,
          [this](int from_index, int to_index) { swap_palette_entries(from_index, to_index); });
  connect(palette_panel_, &PalettePanel::add_from_foreground_requested, this,
          [this] { add_palette_entry_from_foreground(); });
  connect(palette_panel_, &PalettePanel::remove_entry_requested, this,
          [this](int index) { remove_palette_entry(index); });
  connect(palette_panel_, &PalettePanel::copy_color_requested, this, [this](int index) {
    const auto colors = displayed_palette_colors();
    if (index < 0 || index >= static_cast<int>(colors.size())) {
      return;
    }
    const auto name = QColor(colors[static_cast<std::size_t>(index)].red,
                             colors[static_cast<std::size_t>(index)].green,
                             colors[static_cast<std::size_t>(index)].blue)
                          .name();
    QApplication::clipboard()->setText(name);
    statusBar()->showMessage(tr("Copied palette color %1").arg(name));
  });
  connect(palette_panel_, &PalettePanel::preset_requested, this, [this](const QString& id) {
    if (!has_active_document()) {
      return;
    }
    const auto id_utf8 = id.toStdString();
    const auto* preset = patchy::find_builtin_palette_preset(id_utf8);
    if (preset == nullptr) {
      return;
    }
    {
      // Shared with the color picker's palette dropdown: it re-opens on the
      // last chosen palette while the document is not in palette mode.
      auto settings = app_settings();
      settings.setValue(QLatin1String(kColorPickerPaletteChoiceKey), id);
    }
    set_document_palette(std::vector<RgbColor>(preset->colors.begin(), preset->colors.end()), tr("Set palette"),
                         tr("Palette set to %1").arg(translate_data_text(preset->english_name)));
  });
  connect(palette_panel_, &PalettePanel::load_from_file_requested, this, [this] { load_palette_from_file(); });
  connect(palette_panel_, &PalettePanel::save_to_file_requested, this, [this] { save_palette_to_file(); });
  connect(palette_panel_, &PalettePanel::extract_from_image_requested, this,
          [this] { extract_palette_from_image(); });
  connect(palette_panel_, &PalettePanel::convert_requested, this, [this] { convert_document_to_indexed(); });
  // Editing the "Current palette" from a color picker (dropping or pasting a
  // color onto a palette cell) routes through the same undoable document edit
  // as the panel's own entry editing.
  set_color_picker_document_palette_editor([this](int index, QColor color) {
    apply_palette_entry_color(index,
                              RgbColor{static_cast<std::uint8_t>(color.red()),
                                       static_cast<std::uint8_t>(color.green()),
                                       static_cast<std::uint8_t>(color.blue())},
                              true, tr("Edit palette entry"));
    statusBar()->showMessage(tr("Palette index %1 set to %2").arg(index).arg(color.name()));
  });
  set_color_picker_document_palette_name_editor([this](int index, const QString& name) {
    apply_palette_entry_name(index, name);
  });
  specs.push_back(PanelSpec{.id = QStringLiteral("paletteDock"),
                            .object_prefix = QStringLiteral("palette"),
                            .title = tr("Palette"),
                            .content = palette_panel_,
                            .preferred_height = kPaletteDockPreferredHeight});
}

}  // namespace patchy::ui
