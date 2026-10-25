#include "ui/panel_column.hpp"

#include "ui/main_window_shared.hpp"
#include "ui/theme_qss.hpp"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// The dividers between sections: wide enough to grab, the same strip the dock
// separators were.
constexpr int kSectionHandleWidth = 5;

// Hosts a panel so the column can be narrower than the panel's minimum width: the
// panel keeps that width and scrolls horizontally, with the bar at the bottom. It
// never scrolls vertically: the panel always gets the viewport's full height, and
// the minimum height grows by the bar while it shows, so the bar never eats the
// panel's own floor. The panel is placed by hand rather than with widgetResizable,
// which would size a height-for-width panel (Layers) to its preferred height and clip
// its bottom row (GitHub issue 96).
class RightDockOverflowArea final : public QScrollArea {
public:
  explicit RightDockOverflowArea(QWidget* parent) : QScrollArea(parent) {
    setObjectName(QStringLiteral("rightDockOverflowScroll"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }

  [[nodiscard]] QSize sizeHint() const override {
    const auto* content = widget();
    if (content == nullptr) {
      return QScrollArea::sizeHint();
    }
    const auto hint = content->sizeHint().expandedTo(content_minimum());
    return {hint.width() + frameWidth() * 2, hint.height() + frameWidth() * 2};
  }

  [[nodiscard]] QSize minimumSizeHint() const override {
    if (widget() == nullptr) {
      return QScrollArea::minimumSizeHint();
    }
    const auto bar = overflowing_ ? horizontalScrollBar()->sizeHint().height() : 0;
    return {0, content_minimum().height() + bar + frameWidth() * 2};
  }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QScrollArea::resizeEvent(event);
    place_content();
  }

  bool viewportEvent(QEvent* event) override {
    if (event->type() == QEvent::LayoutRequest) {
      // The panel's minimum changed (a retranslated or restyled row).
      place_content();
    }
    return QScrollArea::viewportEvent(event);
  }

private:
  [[nodiscard]] QSize content_minimum() const {
    const auto* content = widget();
    return content->minimumSizeHint().expandedTo(content->minimumSize());
  }

  void place_content() {
    auto* content = widget();
    if (content == nullptr) {
      return;
    }
    const auto minimum = content_minimum();
    const auto overflowing = minimum.width() > width() - frameWidth() * 2;
    const auto viewport_size = viewport()->size();
    content->resize(std::max(viewport_size.width(), minimum.width()), viewport_size.height());
    if (overflowing != overflowing_) {
      overflowing_ = overflowing;
      updateGeometry();
    }
  }

  bool overflowing_{false};
};

// The smallest height a section's widget accepts: its explicit minimum, else its
// layout's.
int widget_floor(const QWidget* widget) {
  return widget->minimumHeight() > 0 ? widget->minimumHeight() : widget->minimumSizeHint().height();
}

// How far above or below its own tab bar a dragged tab still only changes places.
constexpr int kTabBarSlack = 6;
// The dragged title row or tab follows the pointer at this opacity: faint enough to see the
// drop line, the outline and the panels through it.
constexpr double kDragGhostOpacity = 0.5;

// A page that stops being its own section drops the height limits apply_section_state set
// on it there (a collapsed section is pinned to its title row).
void clear_section_limits(QWidget& widget) {
  widget.setMinimumHeight(0);
  widget.setMaximumHeight(QWIDGETSIZE_MAX);
}

// The translucent copy of what a drag carries. A child of the window, never a window of
// its own, and transparent to the mouse.
class PanelDragGhost final : public QWidget {
public:
  explicit PanelDragGhost(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("panelDragGhost"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // The theme gives every plain QWidget an opaque background, which would paint under
    // the translucent copy and hide what the drag passes over.
    setAttribute(Qt::WA_NoSystemBackground);
    set_themed_style(*this, QStringLiteral("QWidget#panelDragGhost { background: transparent; border: none; }"));
  }

  void set_pixmap(QPixmap pixmap) {
    pixmap_ = std::move(pixmap);
    resize(pixmap_.deviceIndependentSize().toSize());
    update();
  }

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    painter.setOpacity(kDragGhostOpacity);
    painter.drawPixmap(0, 0, pixmap_);
  }

private:
  QPixmap pixmap_;
};

// Whether moving tab `from` to just before `slot` leaves the shown tabs as they are:
// the slot is beside the tab, or only closed (hidden) tabs lie between them.
bool tab_move_changes_nothing(const QTabBar& tabs, int from, int slot) {
  const auto first = std::min(from + 1, slot);
  const auto last = std::max(from, slot);
  for (int index = first; index < last; ++index) {
    if (tabs.isTabVisible(index)) {
      return false;
    }
  }
  return true;
}

}  // namespace

struct PanelColumn::Page {
  PanelSpec spec;
  QWidget* widget{nullptr};
  QWidget* header{nullptr};
  QToolButton* toggle{nullptr};
  QLabel* label{nullptr};
  QWidget* body{nullptr};
  bool open{true};
};

struct PanelColumn::Section {
  QWidget* widget{nullptr};
  QTabBar* tabs{nullptr};
  QStackedWidget* stack{nullptr};
  std::vector<std::unique_ptr<Page>> pages;
  bool expanded{true};
  // The section holding the first panel added (Layers): it takes the column's spare
  // height, wherever it has been moved and whatever group it is in.
  bool fills{false};
  // How far lower_floors has lowered the expanded floor.
  int floor_reduction{0};

  [[nodiscard]] Page* front() const {
    if (stack == nullptr) {
      return pages.front().get();
    }
    const auto index = std::clamp(stack->currentIndex(), 0, static_cast<int>(pages.size()) - 1);
    return pages[static_cast<std::size_t>(index)].get();
  }
  [[nodiscard]] bool any_open() const {
    return std::any_of(pages.begin(), pages.end(), [](const auto& page) { return page->open; });
  }
};

PanelColumn::PanelColumn(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("panelColumn"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  splitter_ = new QSplitter(Qt::Vertical, this);
  splitter_->setObjectName(QStringLiteral("panelColumnSplitter"));
  splitter_->setChildrenCollapsible(false);
  splitter_->setHandleWidth(kSectionHandleWidth);
  // The splitter takes the whole height while any section is expanded. With every
  // section collapsed its maximum is the title rows, and the stretch under it takes
  // the rest, so the rows stack from the top (GitHub issue 391).
  layout->addWidget(splitter_, 1);
  layout->addStretch(0);
}

PanelColumn::~PanelColumn() {
  if (dragging_) {
    QApplication::restoreOverrideCursor();  // closed mid-drag
    QApplication::instance()->removeEventFilter(this);
  }
  delete drag_ghost_.data();  // a child of the window, which may outlive the column
}

PanelColumn::Page* PanelColumn::build_page(Section& section, const PanelSpec& spec) {
  auto page = std::make_unique<Page>();
  page->spec = spec;
  page->widget = new QWidget();
  page->widget->setObjectName(spec.id);
  auto* layout = new QVBoxLayout(page->widget);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  page->header = new QWidget(page->widget);
  page->header->setObjectName(spec.object_prefix + QStringLiteral("DockTitle"));
  page->header->setAttribute(Qt::WA_StyledBackground, true);
  page->header->setContextMenuPolicy(Qt::CustomContextMenu);
  auto* header_layout = new QHBoxLayout(page->header);
  header_layout->setContentsMargins(7, 3, 7, 3);
  header_layout->setSpacing(6);
  page->toggle = new QToolButton(page->header);
  page->toggle->setObjectName(spec.object_prefix + QStringLiteral("DockCollapseButton"));
  page->toggle->setProperty("dockCollapseButton", true);
  page->toggle->setAutoRaise(false);
  page->toggle->setCheckable(true);
  page->toggle->setFixedSize(18, 18);
  header_layout->addWidget(page->toggle);
  page->label = new QLabel(spec.title, page->header);
  page->label->setObjectName(spec.object_prefix + QStringLiteral("DockTitleLabel"));
  // A long title clips instead of holding a narrowed column wide.
  page->label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  header_layout->addWidget(page->label, 1);
  layout->addWidget(page->header);
  // A drag on the title row (the label passes its presses up) moves the section.
  page->header->installEventFilter(this);

  if (auto* scroll = qobject_cast<QScrollArea*>(spec.content); scroll != nullptr) {
    page->body = scroll;
  } else {
    auto* overflow = new RightDockOverflowArea(page->widget);
    overflow->setWidget(spec.content);
    page->body = overflow;
  }
  page->body->setParent(page->widget);
  layout->addWidget(page->body, 1);

  const auto id = spec.id;
  connect(page->toggle, &QToolButton::toggled, this, [this, id](bool expanded) { set_panel_expanded(id, expanded); });
  auto* header = page->header;
  connect(header, &QWidget::customContextMenuRequested, this, [this, id, header](const QPoint& position) {
    emit header_context_menu_requested(id, header->mapToGlobal(position), false);
  });
  auto* raw = page.get();
  section.pages.push_back(std::move(page));
  return raw;
}

void PanelColumn::add_panel(const PanelSpec& spec, bool initially_expanded) {
  if (fill_panel_id_.isEmpty()) {
    fill_panel_id_ = spec.id;
  }
  auto section = std::make_unique<Section>();
  section->expanded = initially_expanded;
  auto* page = build_page(*section, spec);
  section->widget = page->widget;
  splitter_->addWidget(section->widget);
  sections_.push_back(std::move(section));
  update_fill_section();
  apply_section_state(*sections_.back());
}

void PanelColumn::add_tab_group(const QString& group_name, const std::vector<PanelSpec>& tabs,
                                bool initially_expanded) {
  if (tabs.empty()) {
    return;
  }
  if (fill_panel_id_.isEmpty()) {
    fill_panel_id_ = tabs.front().id;
  }
  group_name_ = group_name;
  auto section = std::make_unique<Section>();
  section->expanded = initially_expanded;
  build_group_widgets(*section);
  for (const auto& spec : tabs) {
    auto* page = build_page(*section, spec);
    section->stack->addWidget(page->widget);
    section->tabs->addTab(spec.title);
  }
  splitter_->addWidget(section->widget);
  sections_.push_back(std::move(section));
  update_fill_section();
  apply_section_state(*sections_.back());
}

void PanelColumn::build_group_widgets(Section& section) {
  section.widget = new QWidget();
  section.widget->setObjectName(group_name_);
  auto* layout = new QVBoxLayout(section.widget);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto* tabs = new QTabBar(section.widget);
  section.tabs = tabs;
  tabs->setObjectName(group_name_ + QStringLiteral("TabBar"));
  // Fusion paints a palette-light base line beside the tabs (GitHub issue 123).
  tabs->setDrawBase(false);
  tabs->setExpanding(false);
  // The bar spans the section, so the strip beside the tabs is a handle for the whole
  // group (a drag from a tab carries only that tab).
  tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  tabs->setContextMenuPolicy(Qt::CustomContextMenu);
  // The tab bar is the group's handle too: a press still switches tabs, a drag moves.
  tabs->installEventFilter(this);
  section.stack = new QStackedWidget(section.widget);
  layout->addWidget(tabs);
  layout->addWidget(section.stack, 1);
  // The group is looked up by its bar, never captured: groups form and dissolve as
  // panels move.
  connect(tabs, &QTabBar::currentChanged, this, [this, tabs](int index) {
    auto* group = section_for_drag_handle(tabs);
    if (group != nullptr && index >= 0 && index < group->stack->count()) {
      group->stack->setCurrentIndex(index);
    }
  });
  connect(tabs, &QWidget::customContextMenuRequested, this, [this, tabs](const QPoint& position) {
    const auto* group = section_for_drag_handle(tabs);
    if (group == nullptr) {
      return;
    }
    auto index = tabs->tabAt(position);
    if (index < 0) {
      index = tabs->currentIndex();
    }
    if (index < 0 || index >= static_cast<int>(group->pages.size())) {
      return;
    }
    emit header_context_menu_requested(group->pages[static_cast<std::size_t>(index)]->spec.id,
                                       tabs->mapToGlobal(position), true);
  });
}

PanelColumn::Page* PanelColumn::page(const QString& id) const {
  for (const auto& section : sections_) {
    for (const auto& page : section->pages) {
      if (page->spec.id == id) {
        return page.get();
      }
    }
  }
  return nullptr;
}

PanelColumn::Section* PanelColumn::section_of(const QString& id) const {
  for (const auto& section : sections_) {
    for (const auto& page : section->pages) {
      if (page->spec.id == id) {
        return section.get();
      }
    }
  }
  return nullptr;
}

int PanelColumn::section_index(const Section* section) const {
  for (std::size_t index = 0; index < sections_.size(); ++index) {
    if (sections_[index].get() == section) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

QStringList PanelColumn::panel_ids() const {
  QStringList ids;
  for (const auto& section : sections_) {
    for (const auto& page : section->pages) {
      ids << page->spec.id;
    }
  }
  return ids;
}

bool PanelColumn::has_panel(const QString& id) const { return page(id) != nullptr; }

bool PanelColumn::panel_visible(const QString& id) const {
  const auto* found = page(id);
  return found != nullptr && found->open;
}

void PanelColumn::set_panel_visible(const QString& id, bool visible) {
  auto* found = page(id);
  auto* section = section_of(id);
  if (found == nullptr || section == nullptr || found->open == visible) {
    return;
  }
  const bool section_was_shown = section->any_open();
  found->open = visible;
  update_section_visibility(*section);
  if (visible && section->stack != nullptr) {
    bring_to_front(id);
  }
  if (visible && !section_was_shown && section->expanded) {
    // A reopened section gets its working height back from its neighbors.
    grant_preferred_height(*section);
  }
  settle_sizes(*section);
  emit panel_visibility_changed(id, visible);
}

void PanelColumn::update_section_visibility(Section& section) {
  if (section.tabs != nullptr) {
    for (std::size_t index = 0; index < section.pages.size(); ++index) {
      section.tabs->setTabVisible(static_cast<int>(index), section.pages[index]->open);
    }
    // A closed front tab hands the front to the first open one.
    if (!section.front()->open) {
      for (std::size_t index = 0; index < section.pages.size(); ++index) {
        if (section.pages[index]->open) {
          section.tabs->setCurrentIndex(static_cast<int>(index));
          break;
        }
      }
    }
  }
  section.widget->setVisible(section.any_open());
}

bool PanelColumn::panel_expanded(const QString& id) const {
  const auto* section = section_of(id);
  return section != nullptr && section->expanded;
}

void PanelColumn::set_panel_expanded(const QString& id, bool expanded) {
  auto* section = section_of(id);
  if (section == nullptr) {
    return;
  }
  if (section->expanded == expanded) {
    // A toggle clicked into the state it already shows: keep every toggle in step.
    apply_section_state(*section);
    return;
  }
  section->expanded = expanded;
  apply_section_state(*section);
  if (expanded) {
    grant_preferred_height(*section);
  }
  settle_sizes(*section);
  for (const auto& member : section->pages) {
    emit panel_expanded_changed(member->spec.id, expanded);
  }
}

bool PanelColumn::panel_in_front(const QString& id) const {
  const auto* section = section_of(id);
  return section != nullptr && section->front()->spec.id == id;
}

void PanelColumn::bring_to_front(const QString& id) {
  auto* section = section_of(id);
  if (section == nullptr || section->tabs == nullptr) {
    return;
  }
  for (std::size_t index = 0; index < section->pages.size(); ++index) {
    if (section->pages[index]->spec.id == id) {
      section->tabs->setCurrentIndex(static_cast<int>(index));
      return;
    }
  }
}

QStringList PanelColumn::section_panel_ids(const QString& id) const {
  QStringList ids;
  if (const auto* section = section_of(id); section != nullptr) {
    for (const auto& member : section->pages) {
      ids << member->spec.id;
    }
  }
  return ids;
}

bool PanelColumn::in_tab_group(const QString& id) const {
  const auto* section = section_of(id);
  return section != nullptr && section->tabs != nullptr;
}

QWidget* PanelColumn::panel_page(const QString& id) const {
  const auto* found = page(id);
  return found != nullptr ? found->widget : nullptr;
}

QLabel* PanelColumn::title_label(const QString& id) const {
  const auto* found = page(id);
  return found != nullptr ? found->label : nullptr;
}

QToolButton* PanelColumn::collapse_button(const QString& id) const {
  const auto* found = page(id);
  return found != nullptr ? found->toggle : nullptr;
}

QWidget* PanelColumn::section_widget(const QString& id) const {
  const auto* section = section_of(id);
  return section != nullptr ? section->widget : nullptr;
}

QTabBar* PanelColumn::tab_bar(const QString& id) const {
  const auto* section = section_of(id);
  return section != nullptr ? section->tabs : nullptr;
}

QSplitter* PanelColumn::splitter() const { return splitter_; }

void PanelColumn::refresh_tab_titles() {
  for (const auto& section : sections_) {
    if (section->tabs == nullptr) {
      continue;
    }
    for (std::size_t index = 0; index < section->pages.size(); ++index) {
      section->tabs->setTabText(static_cast<int>(index), section->pages[index]->label->text());
    }
  }
}

int PanelColumn::collapsed_height(const Section& section) const {
  const auto header = section.front()->header->sizeHint().height();
  return header + (section.tabs != nullptr ? section.tabs->sizeHint().height() : 0);
}

int PanelColumn::section_floor(const Section& section) const {
  const auto& spec = section.front()->spec;
  if (spec.expanded_minimum_height <= 0) {
    return 0;
  }
  return std::max(collapsed_height(section), spec.expanded_minimum_height - section.floor_reduction);
}

int PanelColumn::section_preferred(const Section& section) const {
  const auto& spec = section.front()->spec;
  const auto floor = std::max(section_floor(section), section.widget->minimumSizeHint().height());
  const auto content = spec.preferred_content_height ? spec.preferred_content_height() : 0;
  const auto preferred = content > 0 ? collapsed_height(section) + content : spec.preferred_height;
  return std::clamp(std::max(preferred, floor), floor, spec.expanded_maximum_height);
}

void PanelColumn::apply_section_state(Section& section) {
  for (const auto& member : section.pages) {
    {
      const QSignalBlocker blocker(member->toggle);
      member->toggle->setChecked(section.expanded);
    }
    member->toggle->setText(section.expanded ? QStringLiteral("v") : QStringLiteral(">"));
    bind_translated_tooltip(member->toggle,
                            section.expanded ? QT_TRANSLATE_NOOP("QObject", "Collapse panel")
                                             : QT_TRANSLATE_NOOP("QObject", "Expand panel"),
                            "QObject");
    apply_bound_translation(member->toggle);
    member->body->setVisible(section.expanded);
  }
  // A collapsed section is exactly its title row (and the group's tab bar): the
  // splitter can neither stretch it nor leave a band under the header.
  if (!section.expanded) {
    const auto height = collapsed_height(section);
    section.widget->setMinimumHeight(height);
    section.widget->setMaximumHeight(height);
  } else {
    section.widget->setMinimumHeight(section_floor(section));
    section.widget->setMaximumHeight(section.front()->spec.expanded_maximum_height);
  }
  section.widget->updateGeometry();
  // A splitter does not pass its children's size changes on to its own parent, so the
  // window would keep a stale minimum (too tall or too short) until an unrelated layout
  // pass. Say it at every level.
  splitter_->updateGeometry();
  updateGeometry();
}

void PanelColumn::grant_preferred_height(Section& section) {
  if (!isVisible() || preferred_heights_pending_) {
    // The first show distributes every section's height at once.
    return;
  }
  auto sizes = splitter_->sizes();
  const auto index = section_index(&section);
  if (index < 0 || index >= sizes.size()) {
    return;
  }
  const auto want = section_preferred(section);
  auto need = want - sizes[index];
  if (need <= 0) {
    return;
  }
  // Taken from the other open, expanded sections, the tallest first, down to their floors.
  std::vector<int> donors;
  for (int other = 0; other < static_cast<int>(sections_.size()); ++other) {
    const auto& candidate = *sections_[static_cast<std::size_t>(other)];
    if (other != index && candidate.expanded && candidate.widget->isVisibleTo(this)) {
      donors.push_back(other);
    }
  }
  std::sort(donors.begin(), donors.end(), [&sizes](int left, int right) { return sizes[left] > sizes[right]; });
  for (const auto donor : donors) {
    const auto floor = widget_floor(sections_[static_cast<std::size_t>(donor)]->widget);
    const auto give = std::min(need, std::max(0, sizes[donor] - floor));
    sizes[donor] -= give;
    sizes[index] += give;
    need -= give;
    if (need <= 0) {
      break;
    }
  }
  splitter_->setSizes(sizes);
}

void PanelColumn::refit_panel_height(const QString& id) {
  auto* target = page(id);
  auto* section = section_of(id);
  if (target == nullptr || section == nullptr) {
    return;
  }
  if (section->expanded && section->front() == target && section->widget->isVisibleTo(this)) {
    fit_section_to_preferred(*section);
  }
}

void PanelColumn::fit_section_to_preferred(Section& section) {
  if (!isVisible() || preferred_heights_pending_) {
    return;
  }
  const auto sizes = splitter_->sizes();
  const auto index = section_index(&section);
  if (index < 0 || index >= sizes.size()) {
    return;
  }
  const auto want = section_preferred(section);
  if (sizes[index] <= want) {
    grant_preferred_height(section);
    return;
  }
  // Shorter: capped at its new height for one settle, the section hands the difference
  // to the nearest expanded sections as a collapse does.
  section.widget->setMaximumHeight(want);
  settle_sizes(section);
  section.widget->setMaximumHeight(section.front()->spec.expanded_maximum_height);
}

void PanelColumn::settle_sizes(const Section& changed) {
  if (!isVisible() || preferred_heights_pending_) {
    return;
  }
  // QSplitter keeps the size it remembers for a slot even after the section's maximum
  // shrinks to its title row, so a collapse or close would leave a band under the header
  // until a divider moved. Clamp every slot to its section's limits and hand the
  // difference to the nearest expanded sections, the ones above first.
  auto sizes = splitter_->sizes();
  const auto count = static_cast<int>(sizes.size());
  int visible = 0;
  int used = 0;
  for (int index = 0; index < count; ++index) {
    const auto* widget = splitter_->widget(index);
    if (widget->isHidden()) {
      sizes[index] = 0;
      continue;
    }
    ++visible;
    sizes[index] = std::clamp(sizes[index], widget_floor(widget), std::max(widget_floor(widget), widget->maximumHeight()));
    used += sizes[index];
  }
  const auto total = splitter_->height() - splitter_->handleWidth() * std::max(0, visible - 1);
  auto spare = total - used;
  const auto origin = section_index(&changed);
  std::vector<int> order;
  for (int index = 0; index < count; ++index) {
    const auto& section = *sections_[static_cast<std::size_t>(index)];
    if (index != origin && section.expanded && !section.widget->isHidden()) {
      order.push_back(index);
    }
  }
  std::stable_sort(order.begin(), order.end(), [origin](int left, int right) {
    const auto left_distance = std::abs(left - origin) * 2 + (left > origin ? 1 : 0);
    const auto right_distance = std::abs(right - origin) * 2 + (right > origin ? 1 : 0);
    return left_distance < right_distance;
  });
  for (const auto index : order) {
    if (spare == 0) {
      break;
    }
    const auto* widget = splitter_->widget(index);
    const auto change = spare > 0 ? std::min(spare, std::max(0, widget->maximumHeight() - sizes[index]))
                                  : std::max(spare, -std::max(0, sizes[index] - widget_floor(widget)));
    sizes[index] += change;
    spare -= change;
  }
  splitter_->setSizes(sizes);
}

QByteArray PanelColumn::save_heights() const { return splitter_->saveState(); }

bool PanelColumn::restore_heights(const QByteArray& state) {
  if (state.isEmpty() || !splitter_->restoreState(state)) {
    return false;
  }
  preferred_heights_pending_ = false;
  return true;
}

void PanelColumn::apply_preferred_heights() {
  if (!isVisible() || splitter_->height() <= 0) {
    preferred_heights_pending_ = true;
    return;
  }
  preferred_heights_pending_ = false;
  const auto handles = splitter_->handleWidth() * std::max(0, splitter_->count() - 1);
  const auto total = std::max(0, splitter_->height() - handles);
  QList<int> sizes;
  int used = 0;
  // The fill section (Layers) takes what is left, wherever it sits; else the first
  // expanded one.
  int filler = -1;
  for (int index = 0; index < static_cast<int>(sections_.size()); ++index) {
    const auto& section = *sections_[static_cast<std::size_t>(index)];
    if (section.expanded && section.widget->isVisibleTo(this) && (filler < 0 || section.fills)) {
      filler = index;
      if (section.fills) {
        break;
      }
    }
  }
  for (int index = 0; index < static_cast<int>(sections_.size()); ++index) {
    const auto& section = *sections_[static_cast<std::size_t>(index)];
    int size = 0;
    if (section.widget->isVisibleTo(this)) {
      if (!section.expanded) {
        size = collapsed_height(section);
      } else if (index == filler) {
        size = 0;
      } else {
        size = section_preferred(section);
      }
    }
    sizes << size;
    used += size;
  }
  if (filler >= 0) {
    const auto& section = *sections_[static_cast<std::size_t>(filler)];
    sizes[filler] = std::max(widget_floor(section.widget), total - used);
  }
  splitter_->setSizes(sizes);
}

int PanelColumn::lower_floors(int excess, int keep) {
  int lowered = 0;
  // Bottom up, the fill section (Layers) last wherever it sits.
  std::vector<Section*> order;
  for (auto it = sections_.rbegin(); it != sections_.rend(); ++it) {
    if (!(*it)->fills) {
      order.push_back(it->get());
    }
  }
  for (const auto& section : sections_) {
    if (section->fills) {
      order.push_back(section.get());
    }
  }
  for (auto it = order.begin(); it != order.end() && excess > 0; ++it) {
    auto& section = **it;
    if (!section.expanded || !section.widget->isVisibleTo(this)) {
      continue;
    }
    const auto& spec = section.front()->spec;
    if (spec.expanded_minimum_height <= 0) {
      continue;
    }
    const auto lowest = collapsed_height(section) + keep;
    const auto current = spec.expanded_minimum_height - section.floor_reduction;
    const auto take = std::min(excess, std::max(0, current - lowest));
    if (take <= 0) {
      continue;
    }
    section.floor_reduction += take;
    excess -= take;
    lowered += take;
    apply_section_state(section);
  }
  return lowered;
}

void PanelColumn::restore_floors() {
  for (const auto& section : sections_) {
    if (section->floor_reduction != 0) {
      section->floor_reduction = 0;
      apply_section_state(*section);
    }
  }
}

QStringList PanelColumn::section_order() const {
  QStringList ids;
  for (const auto& section : sections_) {
    ids << section->pages.front()->spec.id;
  }
  return ids;
}

void PanelColumn::set_section_order(const QStringList& ids) {
  std::vector<std::unique_ptr<Section>> order;
  auto remaining = std::move(sections_);
  sections_.clear();
  const auto take = [&remaining, &order](const Section* wanted) {
    for (auto it = remaining.begin(); it != remaining.end(); ++it) {
      if (it->get() == wanted) {
        order.push_back(std::move(*it));
        remaining.erase(it);
        return;
      }
    }
  };
  for (const auto& id : ids) {
    for (const auto& candidate : remaining) {
      const auto named = std::any_of(candidate->pages.begin(), candidate->pages.end(),
                                     [&id](const auto& page) { return page->spec.id == id; });
      if (named) {
        take(candidate.get());
        break;
      }
    }
  }
  for (auto& rest : remaining) {
    order.push_back(std::move(rest));
  }
  reorder_sections(std::move(order));
}

void PanelColumn::move_section(const QString& id, int slot) {
  if (const auto* section = section_of(id); section != nullptr) {
    move_section_index(section_index(section), slot);
  }
}

void PanelColumn::move_section_index(int from, int slot) {
  const auto count = static_cast<int>(sections_.size());
  slot = std::clamp(slot, 0, count);
  if (from < 0 || from >= count || slot == from || slot == from + 1) {
    return;
  }
  auto order = std::move(sections_);
  sections_.clear();
  auto moved = std::move(order[static_cast<std::size_t>(from)]);
  order.erase(order.begin() + from);
  const auto to = slot > from ? slot - 1 : slot;
  order.insert(order.begin() + to, std::move(moved));
  reorder_sections(std::move(order));
}

void PanelColumn::reorder_sections(std::vector<std::unique_ptr<Section>> order) {
  // Heights go with their sections: read them by widget before the splitter moves any.
  const auto old_sizes = splitter_->sizes();
  std::vector<std::pair<QWidget*, int>> heights;
  for (int index = 0; index < splitter_->count(); ++index) {
    heights.emplace_back(splitter_->widget(index), index < old_sizes.size() ? old_sizes[index] : 0);
  }
  sections_ = std::move(order);
  bool changed = false;
  QList<int> sizes;
  for (int index = 0; index < static_cast<int>(sections_.size()); ++index) {
    auto* widget = sections_[static_cast<std::size_t>(index)]->widget;
    if (splitter_->widget(index) != widget) {
      // An existing child moves to the new index.
      splitter_->insertWidget(index, widget);
      changed = true;
    }
    const auto found = std::find_if(heights.begin(), heights.end(), [widget](const auto& entry) { return entry.first == widget; });
    sizes << (found != heights.end() ? found->second : 0);
  }
  if (!changed) {
    return;
  }
  splitter_->setSizes(sizes);
  emit section_order_changed();
}

PanelColumn::Section* PanelColumn::section_for_drag_handle(const QObject* handle) const {
  for (const auto& section : sections_) {
    if (section->tabs == handle) {
      return section.get();
    }
    for (const auto& member : section->pages) {
      if (member->header == handle) {
        return section.get();
      }
    }
  }
  return nullptr;
}

PanelColumn::Page* PanelColumn::page_for_header(const QObject* header) const {
  for (const auto& section : sections_) {
    for (const auto& member : section->pages) {
      if (member->header == header) {
        return member.get();
      }
    }
  }
  return nullptr;
}

PanelColumn::Drop PanelColumn::drop_at(const QPoint& column_position) const {
  Drop drop;
  auto* source = drag_section_;
  if (source == nullptr || column_position.x() < 0 || column_position.x() >= width()) {
    return drop;  // beside the column (over the canvas, say): a release there puts nothing anywhere
  }
  // Above or below the column, in line with it, lands at its top or bottom (the sections
  // below find the slot): the only way above a tab group that starts at the very top.
  if (drag_tab_ >= 0) {
    // Along its own bar, or just above or below it, a tab only changes places.
    const auto* tabs = source->tabs;
    const QRect bar(tabs->mapTo(this, QPoint(0, 0)), tabs->size());
    if (bar.adjusted(0, -kTabBarSlack, 0, kTabBarSlack).contains(column_position)) {
      const auto slot = tab_drop_slot_at(*source, tabs->mapFrom(this, column_position));
      if (!tab_move_changes_nothing(*tabs, drag_tab_, slot)) {
        drop.kind = Drop::Kind::TabOrder;
        drop.slot = slot;
      }
      return drop;
    }
  }
  // A section's header (its tab bar and title row) takes the panel in, but for a thin band
  // at its top and bottom edges, which drops between sections. Below the header a drop
  // lands above the section's middle or below it.
  auto slot = static_cast<int>(sections_.size());
  Section* target = nullptr;
  int tab_slot = -1;
  for (int index = 0; index < static_cast<int>(sections_.size()); ++index) {
    auto* section = sections_[static_cast<std::size_t>(index)].get();
    if (section->widget->isHidden()) {
      continue;
    }
    const QRect rect(section->widget->mapTo(this, QPoint(0, 0)), section->widget->size());
    const auto y = column_position.y();
    if (y < rect.top()) {
      slot = index;  // on the divider above it
      break;
    }
    if (y > rect.bottom()) {
      continue;
    }
    const auto header = collapsed_height(*section);
    const auto edge = std::clamp(header / 4, 3, 8);
    if (y < rect.top() + edge) {
      slot = index;
    } else if (y < rect.top() + header - edge) {
      target = section;
      if (section->tabs != nullptr) {
        const QRect bar(section->tabs->mapTo(this, QPoint(0, 0)), section->tabs->size());
        if (bar.contains(column_position)) {
          tab_slot = tab_drop_slot_at(*section, section->tabs->mapFrom(this, column_position));
        }
      }
    } else if (!section->expanded) {
      slot = index + 1;
    } else {
      slot = y < rect.center().y() ? index : index + 1;
    }
    break;
  }
  if (target != nullptr) {
    if (target != source) {  // onto its own header: nothing
      drop.kind = Drop::Kind::Merge;
      drop.target = target;
      drop.tab_slot = tab_slot;
    }
    return drop;
  }
  const auto from = section_index(source);
  if (drag_tab_ < 0 && (slot == from || slot == from + 1)) {
    return drop;  // a section dropped where it is
  }
  drop.kind = Drop::Kind::Slot;
  drop.slot = slot;
  return drop;
}

int PanelColumn::tab_drop_slot_at(const Section& section, const QPoint& bar_position) const {
  // Before the first shown tab whose middle lies past the pointer; past every middle: the end.
  const auto* tabs = section.tabs;
  const bool right_to_left = tabs->isRightToLeft();
  for (int index = 0; index < tabs->count(); ++index) {
    if (!tabs->isTabVisible(index)) {
      continue;
    }
    const auto middle = tabs->tabRect(index).center().x();
    if (right_to_left ? bar_position.x() > middle : bar_position.x() < middle) {
      return index;
    }
  }
  return tabs->count();
}

void PanelColumn::show_drop(const Drop& drop) {
  if (drop_indicator_ == nullptr) {
    drop_indicator_ = new QWidget(this);
    drop_indicator_->setObjectName(QStringLiteral("panelDropIndicator"));
    drop_indicator_->setAttribute(Qt::WA_TransparentForMouseEvents);
    drop_indicator_->setAttribute(Qt::WA_StyledBackground, true);
    // The Layers list's insertion line.
    set_themed_style(*drop_indicator_, QStringLiteral(
        "QWidget#panelDropIndicator { background: @drop_indicator_bg; "
        "border: 1px solid @drop_indicator_border; border-radius: 2px; }"));
    // Outlines the header a dropped panel joins.
    merge_indicator_ = new QWidget(this);
    merge_indicator_->setObjectName(QStringLiteral("panelMergeIndicator"));
    merge_indicator_->setAttribute(Qt::WA_TransparentForMouseEvents);
    merge_indicator_->setAttribute(Qt::WA_StyledBackground, true);
    set_themed_style(*merge_indicator_, QStringLiteral(
        "QWidget#panelMergeIndicator { background: transparent; "
        "border: 2px solid @drop_indicator_bg; border-radius: 3px; }"));
  }
  drop_indicator_->hide();
  merge_indicator_->hide();
  switch (drop.kind) {
    case Drop::Kind::Slot:
      show_slot_line(drop.slot);
      break;
    case Drop::Kind::TabOrder:
      show_tab_line(*drag_section_, drop.slot);
      break;
    case Drop::Kind::Merge:
      show_merge_frame(*drop.target);
      if (drop.tab_slot >= 0) {
        show_tab_line(*drop.target, drop.tab_slot);
      }
      break;
    case Drop::Kind::None:
      break;  // dropping here changes nothing
  }
}

void PanelColumn::show_slot_line(int slot) {
  constexpr int kThickness = 4;
  int y = 0;
  bool above_a_section = false;
  for (int index = slot; index < static_cast<int>(sections_.size()); ++index) {
    const auto* widget = sections_[static_cast<std::size_t>(index)]->widget;
    if (!widget->isHidden()) {
      // Centered on the divider above it (the top edge for the first section).
      y = widget->mapTo(this, QPoint(0, 0)).y() - (splitter_->handleWidth() + kThickness) / 2;
      above_a_section = true;
      break;
    }
  }
  if (!above_a_section) {
    // The bottom: just inside the column's lower edge.
    for (auto it = sections_.rbegin(); it != sections_.rend(); ++it) {
      const auto* widget = (*it)->widget;
      if (!widget->isHidden()) {
        y = widget->mapTo(this, QPoint(0, widget->height())).y() - kThickness;
        break;
      }
    }
  }
  y = std::clamp(y, 0, std::max(0, height() - kThickness));
  drop_indicator_->setGeometry(2, y, std::max(0, width() - 4), kThickness);
  drop_indicator_->show();
  drop_indicator_->raise();
}

void PanelColumn::show_tab_line(const Section& section, int slot) {
  const auto* tabs = section.tabs;
  const bool right_to_left = tabs->isRightToLeft();
  // The leading edge of the tab it would land before, or the trailing edge of the last
  // shown tab.
  int x = 0;
  if (slot < tabs->count()) {
    const auto rect = tabs->tabRect(slot);
    x = right_to_left ? rect.right() + 1 : rect.left();
  } else {
    for (int index = tabs->count() - 1; index >= 0; --index) {
      if (tabs->isTabVisible(index)) {
        const auto rect = tabs->tabRect(index);
        x = right_to_left ? rect.left() : rect.right() + 1;
        break;
      }
    }
  }
  constexpr int kThickness = 4;
  const auto origin = tabs->mapTo(this, QPoint(x, 0));
  const auto left = std::clamp(origin.x() - kThickness / 2, 0, std::max(0, width() - kThickness));
  drop_indicator_->setGeometry(left, origin.y(), kThickness, tabs->height());
  drop_indicator_->show();
  drop_indicator_->raise();
}

void PanelColumn::show_merge_frame(const Section& section) {
  const auto origin = section.widget->mapTo(this, QPoint(0, 0));
  merge_indicator_->setGeometry(origin.x(), origin.y(), section.widget->width(), collapsed_height(section));
  merge_indicator_->show();
  merge_indicator_->raise();
}

void PanelColumn::start_drag_ghost() {
  // What the drag carries: the tab, or the bar the section was picked up by (its title row
  // or its tab bar), no more, so the ghost hides little of where it goes.
  auto& source = *drag_section_;
  QPixmap pixmap;
  if (drag_tab_ >= 0) {
    const auto rect = source.tabs->tabRect(drag_tab_);
    pixmap = source.tabs->grab(rect);
    ghost_hotspot_ = source.tabs->mapFromGlobal(drag_press_global_) - rect.topLeft();
  } else {
    auto* handle = drag_handle_ != nullptr ? drag_handle_ : source.front()->header;
    QRect area = handle->rect();
    if (handle == source.tabs) {
      // The tabs themselves, not the empty bar beside them.
      QRect tabs_area;
      for (int index = 0; index < source.tabs->count(); ++index) {
        if (source.tabs->isTabVisible(index)) {
          tabs_area = tabs_area.united(source.tabs->tabRect(index));
        }
      }
      if (!tabs_area.isEmpty()) {
        area = tabs_area;
      }
    }
    pixmap = handle->grab(area);
    ghost_hotspot_ = handle->mapFromGlobal(drag_press_global_) - area.topLeft();
  }
  auto* host = window();
  if (drag_ghost_ == nullptr || drag_ghost_->parentWidget() != host) {
    delete drag_ghost_.data();
    drag_ghost_ = new PanelDragGhost(host);
  }
  static_cast<PanelDragGhost*>(drag_ghost_.data())->set_pixmap(std::move(pixmap));
  ghost_hotspot_.setX(std::clamp(ghost_hotspot_.x(), 0, std::max(0, drag_ghost_->width() - 1)));
  ghost_hotspot_.setY(std::clamp(ghost_hotspot_.y(), 0, std::max(0, drag_ghost_->height() - 1)));
  drag_ghost_->show();
}

void PanelColumn::move_drag_ghost(const QPoint& global_position) {
  if (drag_ghost_ == nullptr) {
    return;
  }
  drag_ghost_->move(drag_ghost_->parentWidget()->mapFromGlobal(global_position) - ghost_hotspot_);
  drag_ghost_->raise();
}

void PanelColumn::end_section_drag(bool apply) {
  auto* section = drag_section_;
  const auto tab = drag_tab_;
  const auto drop = drop_;
  const auto was_dragging = dragging_;
  drag_section_ = nullptr;
  drag_handle_ = nullptr;
  press_tab_ = -1;
  drag_tab_ = -1;
  drop_ = Drop{};
  dragging_ = false;
  for (auto* indicator : {drop_indicator_, merge_indicator_, drag_ghost_.data()}) {
    if (indicator != nullptr) {
      indicator->hide();
    }
  }
  if (!was_dragging) {
    return;
  }
  QApplication::restoreOverrideCursor();
  QApplication::instance()->removeEventFilter(this);
  if (apply && section != nullptr) {
    apply_drop(*section, tab, drop);
  }
}

void PanelColumn::apply_drop(Section& source, int tab, const Drop& drop) {
  const auto dragged = tab >= 0 ? source.pages[static_cast<std::size_t>(tab)]->spec.id : source.front()->spec.id;
  switch (drop.kind) {
    case Drop::Kind::TabOrder:
      if (move_tab_index(source, tab, drop.slot)) {
        emit tab_groups_changed();
      }
      return;
    case Drop::Kind::Slot:
      if (tab < 0) {
        move_section_index(section_index(&source), drop.slot);
        return;
      }
      break;
    case Drop::Kind::Merge:
      break;
    case Drop::Kind::None:
      return;
  }
  // Regrouping reparents panels and can delete a tab bar, and the drop arrives inside the
  // dragged header's or tab bar's own event: do it once that event is done.
  const auto kind = drop.kind;
  const auto slot = drop.slot;
  const auto target = kind == Drop::Kind::Merge ? drop.target->pages.front()->spec.id : QString();
  const auto tab_slot = drop.tab_slot;
  const bool whole_section = tab < 0;
  QMetaObject::invokeMethod(
      this,
      [this, kind, dragged, target, slot, tab_slot, whole_section] {
        if (kind == Drop::Kind::Slot) {
          separate_panel(dragged, slot);
        } else if (whole_section) {
          merge_section(dragged, target, tab_slot);
        } else {
          merge_panel(dragged, target, tab_slot);
        }
      },
      Qt::QueuedConnection);
}

QList<QStringList> PanelColumn::tab_groups() const {
  QList<QStringList> groups;
  for (const auto& section : sections_) {
    if (section->tabs == nullptr) {
      continue;
    }
    QStringList ids;
    for (const auto& member : section->pages) {
      ids << member->spec.id;
    }
    groups << ids;
  }
  return groups;
}

void PanelColumn::set_tab_order(const QStringList& ids) {
  Section* section = nullptr;
  for (const auto& id : ids) {
    if (auto* candidate = section_of(id); candidate != nullptr && candidate->tabs != nullptr) {
      section = candidate;
      break;
    }
  }
  if (section == nullptr) {
    return;
  }
  if (order_tabs(*section, ids)) {
    emit tab_groups_changed();
  }
}

bool PanelColumn::order_tabs(Section& section, const QStringList& ids) {
  // Each named tab of the group in turn to the next place; the rest follow.
  bool changed = false;
  int place = 0;
  for (const auto& id : ids) {
    for (int index = place; index < static_cast<int>(section.pages.size()); ++index) {
      if (section.pages[static_cast<std::size_t>(index)]->spec.id == id) {
        changed = move_tab_index(section, index, place) || changed;
        ++place;
        break;
      }
    }
  }
  return changed;
}

void PanelColumn::move_tab(const QString& id, int slot) {
  auto* section = section_of(id);
  if (section == nullptr || section->tabs == nullptr) {
    return;
  }
  for (std::size_t index = 0; index < section->pages.size(); ++index) {
    if (section->pages[index]->spec.id == id) {
      if (move_tab_index(*section, static_cast<int>(index), slot)) {
        emit tab_groups_changed();
      }
      return;
    }
  }
}

bool PanelColumn::move_tab_index(Section& section, int from, int slot) {
  const auto count = static_cast<int>(section.pages.size());
  slot = std::clamp(slot, 0, count);
  if (section.tabs == nullptr || from < 0 || from >= count || slot == from || slot == from + 1) {
    return false;
  }
  const auto to = slot > from ? slot - 1 : slot;
  // The pages, the stack and the tab bar share one index per panel: move all three.
  auto moved = std::move(section.pages[static_cast<std::size_t>(from)]);
  section.pages.erase(section.pages.begin() + from);
  section.pages.insert(section.pages.begin() + to, std::move(moved));
  auto* front = section.stack->currentWidget();
  auto* focus = QApplication::focusWidget();
  const bool focus_in_stack = focus != nullptr && section.stack->isAncestorOf(focus);
  auto* page_widget = section.stack->widget(from);
  section.stack->removeWidget(page_widget);
  section.stack->insertWidget(to, page_widget);
  section.stack->setCurrentWidget(front);
  // moveTab keeps the same tab current; its currentChanged then names the index the
  // stack already shows.
  section.tabs->moveTab(from, to);
  if (focus_in_stack && focus->isVisible()) {
    focus->setFocus(Qt::OtherFocusReason);  // taking the page out of the stack hid it
  }
  return true;
}

void PanelColumn::set_tab_groups(const QList<QStringList>& groups) {
  // The groups asked for: known panels, each in one group only, two or more per group.
  QList<QStringList> wanted;
  QStringList taken;
  for (const auto& group : groups) {
    QStringList ids;
    for (const auto& id : group) {
      if (has_panel(id) && !taken.contains(id)) {
        ids << id;
        taken << id;
      }
    }
    if (ids.size() >= 2) {
      wanted << ids;
    }
  }
  bool changed = false;
  std::vector<bool> formed(static_cast<std::size_t>(wanted.size()), false);
  // A group that already holds one wanted group's panels stays and only orders its tabs.
  // Every other group comes apart in place, each tab a section of its own, in tab order.
  for (int index = 0; index < static_cast<int>(sections_.size()); ++index) {
    auto& section = *sections_[static_cast<std::size_t>(index)];
    if (section.tabs == nullptr) {
      continue;
    }
    QStringList members;
    for (const auto& member : section.pages) {
      members << member->spec.id;
    }
    auto sorted_members = members;
    sorted_members.sort();
    int match = -1;
    for (int candidate = 0; candidate < wanted.size(); ++candidate) {
      auto sorted_wanted = wanted[candidate];
      sorted_wanted.sort();
      if (!formed[static_cast<std::size_t>(candidate)] && sorted_wanted == sorted_members) {
        match = candidate;
        break;
      }
    }
    if (match >= 0) {
      formed[static_cast<std::size_t>(match)] = true;
      changed = order_tabs(section, wanted[match]) || changed;
      continue;
    }
    const bool expanded = section.expanded;
    while (section.tabs != nullptr) {  // the last tab dissolves the group into a section
      insert_section(detach_page(section.pages.back()->spec.id), index + 1, expanded);
    }
    changed = true;
  }
  for (int group = 0; group < wanted.size(); ++group) {
    if (formed[static_cast<std::size_t>(group)]) {
      continue;
    }
    const auto& ids = wanted[group];
    auto* target = section_of(ids.front());
    for (int member = 1; member < ids.size(); ++member) {
      insert_page(*target, detach_page(ids[member]), -1);
    }
    bring_to_front(ids.front());
    changed = true;
  }
  if (changed) {
    finish_regroup(*sections_.front());
  }
}

void PanelColumn::merge_panel(const QString& id, const QString& target_id, int tab_slot) {
  const auto* source = section_of(id);
  auto* target = section_of(target_id);
  if (source == nullptr || target == nullptr || source == target) {
    return;
  }
  insert_page(*target, detach_page(id), tab_slot);
  finish_regroup(*target);
}

void PanelColumn::merge_section(const QString& id, const QString& target_id, int tab_slot) {
  const auto* source = section_of(id);
  auto* target = section_of(target_id);
  if (source == nullptr || target == nullptr || source == target) {
    return;
  }
  const auto front = source->front()->spec.id;
  auto slot = tab_slot;
  for (const auto& member : section_panel_ids(id)) {
    insert_page(*target, detach_page(member), slot);
    if (slot >= 0) {
      ++slot;
    }
  }
  bring_to_front(front);
  finish_regroup(*target);
}

void PanelColumn::separate_panel(const QString& id, int slot) {
  const auto* source = section_of(id);
  if (source == nullptr) {
    return;
  }
  if (source->tabs == nullptr) {
    move_section(id, slot);
    return;
  }
  // The group keeps at least one tab, so it stays a section and the slots stand.
  const bool expanded = source->expanded;
  auto& section = insert_section(detach_page(id), slot, expanded);
  apply_section_state(section);
  if (expanded && section.widget->isVisibleTo(this)) {
    grant_preferred_height(section);
  }
  finish_regroup(section);
}

void PanelColumn::make_group(Section& section) {
  if (section.tabs != nullptr) {
    return;
  }
  // The section's page goes into a new group in the same splitter slot, at the same height.
  auto* page_widget = section.widget;
  const auto index = splitter_->indexOf(page_widget);
  build_group_widgets(section);
  splitter_->replaceWidget(index, section.widget);
  clear_section_limits(*page_widget);
  const auto& page = *section.pages.front();
  section.stack->addWidget(page_widget);
  section.tabs->addTab(page.label->text());
  page_widget->show();
}

void PanelColumn::dissolve_group(Section& section) {
  if (section.tabs == nullptr || section.pages.size() != 1) {
    return;
  }
  // A group of one is a plain section again: its page takes the group's splitter slot.
  auto* container = section.widget;
  auto* tabs = section.tabs;
  auto* page_widget = section.pages.front()->widget;
  section.stack->removeWidget(page_widget);
  section.widget = page_widget;
  section.tabs = nullptr;
  section.stack = nullptr;
  splitter_->replaceWidget(splitter_->indexOf(container), page_widget);
  tabs->disconnect(this);
  tabs->removeEventFilter(this);
  container->hide();
  container->deleteLater();
  page_widget->setVisible(section.any_open());
}

std::unique_ptr<PanelColumn::Page> PanelColumn::detach_page(const QString& id) {
  auto* section = section_of(id);
  if (section == nullptr) {
    return {};
  }
  if (section->tabs == nullptr) {
    // A section of its own goes with its panel.
    const auto index = static_cast<std::size_t>(section_index(section));
    auto owned = std::move(sections_[index]);
    sections_.erase(sections_.begin() + static_cast<std::ptrdiff_t>(index));
    auto page = std::move(owned->pages.front());
    page->widget->hide();
    page->widget->setParent(nullptr);
    clear_section_limits(*page->widget);
    return page;
  }
  const auto found = std::find_if(section->pages.begin(), section->pages.end(),
                                  [&id](const auto& member) { return member->spec.id == id; });
  const auto index = static_cast<int>(found - section->pages.begin());
  auto page = std::move(*found);
  section->pages.erase(found);
  // The stack first: the bar's currentChanged then names the index the stack shows.
  section->stack->removeWidget(page->widget);
  section->tabs->removeTab(index);
  page->widget->hide();
  page->widget->setParent(nullptr);
  if (section->pages.size() == 1) {
    dissolve_group(*section);
  } else {
    update_section_visibility(*section);
  }
  return page;
}

void PanelColumn::insert_page(Section& target, std::unique_ptr<Page> page, int tab_slot) {
  if (page == nullptr) {
    return;
  }
  make_group(target);
  const auto count = static_cast<int>(target.pages.size());
  const auto slot = tab_slot < 0 ? count : std::min(tab_slot, count);
  auto* raw = page.get();
  target.pages.insert(target.pages.begin() + slot, std::move(page));
  target.stack->insertWidget(slot, raw->widget);
  target.tabs->insertTab(slot, raw->label->text());
  target.tabs->setTabVisible(slot, raw->open);
  if (raw->open) {
    target.tabs->setCurrentIndex(slot);  // a panel joins in front
  }
  target.stack->setCurrentIndex(target.tabs->currentIndex());
  update_section_visibility(target);
}

PanelColumn::Section& PanelColumn::insert_section(std::unique_ptr<Page> page, int slot, bool expanded) {
  auto section = std::make_unique<Section>();
  section->expanded = expanded;
  section->widget = page->widget;
  section->pages.push_back(std::move(page));
  slot = std::clamp(slot, 0, static_cast<int>(sections_.size()));
  splitter_->insertWidget(slot, section->widget);
  auto& raw = *section;
  sections_.insert(sections_.begin() + slot, std::move(section));
  raw.widget->setVisible(raw.any_open());
  return raw;
}

void PanelColumn::update_fill_section() {
  for (int index = 0; index < static_cast<int>(sections_.size()); ++index) {
    auto& section = *sections_[static_cast<std::size_t>(index)];
    section.fills = std::any_of(section.pages.begin(), section.pages.end(),
                                [this](const auto& member) { return member->spec.id == fill_panel_id_; });
    splitter_->setStretchFactor(index, section.fills ? 1 : 0);
  }
}

void PanelColumn::finish_regroup(const Section& changed) {
  expanded_by_click_ = nullptr;  // may name a section that went
  update_fill_section();
  for (const auto& section : sections_) {
    apply_section_state(*section);
    update_section_visibility(*section);
  }
  settle_sizes(changed);
  emit tab_groups_changed();
  emit section_order_changed();
}

bool PanelColumn::eventFilter(QObject* watched, QEvent* event) {
  switch (event->type()) {
    case QEvent::MouseButtonPress: {
      const auto* mouse = static_cast<QMouseEvent*>(event);
      if (dragging_) {
        end_section_drag(false);
        return true;
      }
      if (mouse->button() == Qt::LeftButton) {
        drag_section_ = section_for_drag_handle(watched);
        drag_handle_ = drag_section_ != nullptr ? qobject_cast<QWidget*>(watched) : nullptr;
        drag_press_global_ = mouse->globalPosition().toPoint();
        press_tab_ = drag_section_ != nullptr && drag_section_->tabs == watched
                         ? drag_section_->tabs->tabAt(mouse->position().toPoint())
                         : -1;
        click_page_ = page_for_header(watched);
        expanded_by_click_ = nullptr;
      }
      break;
    }
    case QEvent::MouseButtonDblClick: {
      // The second press of a double click on a title row. An expanded section collapses,
      // unless the first click just expanded it (a double click expands a collapsed one).
      const auto* mouse = static_cast<QMouseEvent*>(event);
      auto* header = page_for_header(watched);
      if (mouse->button() != Qt::LeftButton || header == nullptr) {
        break;
      }
      auto* section = section_for_drag_handle(watched);
      const bool just_expanded = expanded_by_click_ == section;
      expanded_by_click_ = nullptr;
      click_page_ = nullptr;
      drag_section_ = nullptr;
      if (!just_expanded && section->expanded) {
        // The collapse button's own path, so the saved layout sees the same change.
        header->toggle->setChecked(false);
      }
      return true;
    }
    case QEvent::MouseMove: {
      const auto* mouse = static_cast<QMouseEvent*>(event);
      if (drag_section_ == nullptr || (mouse->buttons() & Qt::LeftButton) == 0 ||
          (!dragging_ && section_for_drag_handle(watched) != drag_section_)) {
        break;
      }
      const auto global = mouse->globalPosition().toPoint();
      if (!dragging_) {
        const auto delta = global - drag_press_global_;
        if (delta.manhattanLength() < QApplication::startDragDistance()) {
          break;
        }
        // Started on a tab, the drag carries that tab, whichever way it goes; from a title
        // row or the bar beside the tabs it carries the section.
        drag_tab_ = press_tab_;
        dragging_ = true;
        QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        // Escape cancels, whichever widget has the keyboard.
        QApplication::instance()->installEventFilter(this);
        start_drag_ghost();
      }
      move_drag_ghost(global);
      drop_ = drop_at(mapFromGlobal(global));
      show_drop(drop_);
      return true;
    }
    case QEvent::MouseButtonRelease: {
      const auto* mouse = static_cast<QMouseEvent*>(event);
      if (mouse->button() != Qt::LeftButton) {
        break;
      }
      if (dragging_) {
        click_page_ = nullptr;
        const bool tab_drag = drag_tab_ >= 0;
        end_section_drag(true);
        // After a tab move the bar still gets the release, so it clears its pressed tab.
        return !tab_drag;
      }
      drag_section_ = nullptr;
      press_tab_ = -1;
      // A click (no drag) on a collapsed section's title row expands it, through the
      // collapse button's own path. A release off the row is no click.
      auto* clicked = click_page_;
      click_page_ = nullptr;
      if (clicked != nullptr && watched == clicked->header &&
          clicked->header->rect().contains(clicked->header->mapFromGlobal(mouse->globalPosition().toPoint()))) {
        auto* section = section_for_drag_handle(watched);
        if (section != nullptr && !section->expanded) {
          expanded_by_click_ = section;
          clicked->toggle->setChecked(true);
        }
      }
      break;
    }
    case QEvent::KeyPress:
      if (dragging_ && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        end_section_drag(false);
        return true;
      }
      break;
    default:
      break;
  }
  return QWidget::eventFilter(watched, event);
}

void PanelColumn::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (preferred_heights_pending_ && splitter_->height() > 0) {
    apply_preferred_heights();
  }
}

void PanelColumn::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (preferred_heights_pending_ && isVisible() && splitter_->height() > 0) {
    apply_preferred_heights();
  }
}

void PanelColumn::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
    // The collapsed height is the styled title row's; restyling changes it.
    for (const auto& section : sections_) {
      apply_section_state(*section);
    }
  }
}

}  // namespace patchy::ui
