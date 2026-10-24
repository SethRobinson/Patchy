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
  // The first section added (the Layers group): it takes the column's spare height,
  // wherever it has been moved.
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
  }
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
  auto section = std::make_unique<Section>();
  section->expanded = initially_expanded;
  section->fills = sections_.empty();
  auto* page = build_page(*section, spec);
  section->widget = page->widget;
  splitter_->addWidget(section->widget);
  // The first section takes the column's spare height as the window grows.
  splitter_->setStretchFactor(splitter_->count() - 1, sections_.empty() ? 1 : 0);
  apply_section_state(*section);
  sections_.push_back(std::move(section));
}

void PanelColumn::add_tab_group(const QString& group_name, const std::vector<PanelSpec>& tabs,
                                bool initially_expanded) {
  if (tabs.empty()) {
    return;
  }
  auto section = std::make_unique<Section>();
  section->expanded = initially_expanded;
  section->fills = sections_.empty();
  section->widget = new QWidget();
  section->widget->setObjectName(group_name);
  auto* layout = new QVBoxLayout(section->widget);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  section->tabs = new QTabBar(section->widget);
  section->tabs->setObjectName(group_name + QStringLiteral("TabBar"));
  // Fusion paints a palette-light base line beside the tabs (GitHub issue 123).
  section->tabs->setDrawBase(false);
  section->tabs->setExpanding(false);
  section->tabs->setContextMenuPolicy(Qt::CustomContextMenu);
  // The tab bar is the group's handle too: a press still switches tabs, a drag moves.
  section->tabs->installEventFilter(this);
  section->stack = new QStackedWidget(section->widget);
  layout->addWidget(section->tabs);
  layout->addWidget(section->stack, 1);
  for (const auto& spec : tabs) {
    auto* page = build_page(*section, spec);
    section->stack->addWidget(page->widget);
    section->tabs->addTab(spec.title);
  }
  auto* raw = section.get();
  connect(section->tabs, &QTabBar::currentChanged, this, [raw](int index) {
    if (index >= 0 && index < raw->stack->count()) {
      raw->stack->setCurrentIndex(index);
    }
  });
  connect(section->tabs, &QWidget::customContextMenuRequested, this, [this, raw](const QPoint& position) {
    auto index = raw->tabs->tabAt(position);
    if (index < 0) {
      index = raw->tabs->currentIndex();
    }
    if (index < 0 || index >= static_cast<int>(raw->pages.size())) {
      return;
    }
    emit header_context_menu_requested(raw->pages[static_cast<std::size_t>(index)]->spec.id,
                                       raw->tabs->mapToGlobal(position), true);
  });
  splitter_->addWidget(section->widget);
  splitter_->setStretchFactor(splitter_->count() - 1, sections_.empty() ? 1 : 0);
  apply_section_state(*section);
  sections_.push_back(std::move(section));
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
  return std::clamp(std::max(spec.preferred_height, floor), floor, spec.expanded_maximum_height);
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

int PanelColumn::drop_slot_at(const QPoint& column_position) const {
  // Above a section's middle drops above it; below every middle drops at the bottom.
  for (int index = 0; index < static_cast<int>(sections_.size()); ++index) {
    const auto* widget = sections_[static_cast<std::size_t>(index)]->widget;
    if (widget->isHidden()) {
      continue;
    }
    const QRect rect(widget->mapTo(this, QPoint(0, 0)), widget->size());
    if (column_position.y() < rect.center().y()) {
      return index;
    }
  }
  return static_cast<int>(sections_.size());
}

void PanelColumn::show_drop_indicator(int slot) {
  drop_slot_ = slot;
  if (drop_indicator_ == nullptr) {
    drop_indicator_ = new QWidget(this);
    drop_indicator_->setObjectName(QStringLiteral("panelDropIndicator"));
    drop_indicator_->setAttribute(Qt::WA_TransparentForMouseEvents);
    drop_indicator_->setAttribute(Qt::WA_StyledBackground, true);
    // The Layers list's insertion line.
    set_themed_style(*drop_indicator_, QStringLiteral(
        "QWidget#panelDropIndicator { background: @drop_indicator_bg; "
        "border: 1px solid @drop_indicator_border; border-radius: 2px; }"));
  }
  const auto from = section_index(drag_section_);
  if (slot == from || slot == from + 1) {
    drop_indicator_->hide();  // dropping here changes nothing
    return;
  }
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

void PanelColumn::end_section_drag(bool apply) {
  auto* section = drag_section_;
  const auto slot = drop_slot_;
  const auto was_dragging = dragging_;
  drag_section_ = nullptr;
  drop_slot_ = -1;
  dragging_ = false;
  if (drop_indicator_ != nullptr) {
    drop_indicator_->hide();
  }
  if (!was_dragging) {
    return;
  }
  QApplication::restoreOverrideCursor();
  QApplication::instance()->removeEventFilter(this);
  if (apply && section != nullptr && slot >= 0) {
    move_section_index(section_index(section), slot);
  }
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
        drag_press_global_ = mouse->globalPosition().toPoint();
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
        if ((global - drag_press_global_).manhattanLength() < QApplication::startDragDistance()) {
          break;
        }
        dragging_ = true;
        QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        // Escape cancels, whichever widget has the keyboard.
        QApplication::instance()->installEventFilter(this);
      }
      show_drop_indicator(drop_slot_at(mapFromGlobal(global)));
      return true;
    }
    case QEvent::MouseButtonRelease: {
      const auto* mouse = static_cast<QMouseEvent*>(event);
      if (mouse->button() != Qt::LeftButton) {
        break;
      }
      if (dragging_) {
        click_page_ = nullptr;
        end_section_drag(true);
        return true;
      }
      drag_section_ = nullptr;
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
