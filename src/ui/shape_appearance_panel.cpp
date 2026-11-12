#include "ui/shape_appearance_panel.hpp"

#include "ui/app_settings.hpp"
#include "ui/brush_tool_window.hpp"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>

namespace patchy::ui {

namespace {

// A page of the window. Its size hint is the size that shows every row (a scroll area's
// own hint is capped at a few lines), so the window's layout sizes itself from it.
class ShapeAppearancePage final : public QWidget {
public:
  using QWidget::QWidget;

  void set_fitted_size(QSize size) {
    fitted_ = size;
    updateGeometry();
  }
  [[nodiscard]] QSize sizeHint() const override { return fitted_.isValid() ? fitted_ : QWidget::sizeHint(); }

private:
  QSize fitted_;
};

bool same_units(const DocumentFieldUnits& a, const DocumentFieldUnits& b) {
  return a.display_unit == b.display_unit && a.ppi == b.ppi && a.document_width == b.document_width &&
         a.document_height == b.document_height;
}

}  // namespace

bool ShapeAppearanceTarget::same_layers_as(const ShapeAppearanceTarget& other) const {
  return session_id == other.session_id && ids == other.ids && selected_count == other.selected_count;
}

bool ShapeAppearanceTarget::same_page_as(const ShapeAppearanceTarget& other) const {
  return same_layers_as(other) && revisions == other.revisions && same_units(options.units, other.options.units);
}

ShapeAppearancePanel::ShapeAppearancePanel(QWidget* owner, ShapeAppearancePanelHost host)
    : QObject(owner), host_(std::move(host)), owner_(owner) {
  rebuild_timer_ = new QTimer(this);
  rebuild_timer_->setSingleShot(true);
  rebuild_timer_->setInterval(0);
  connect(rebuild_timer_, &QTimer::timeout, this, [this] { rebuild(); });
  // A gesture's values arrive at most a frame or two apart; a pause this long is a new one.
  idle_timer_ = new QTimer(this);
  idle_timer_->setSingleShot(true);
  idle_timer_->setInterval(600);
  connect(idle_timer_, &QTimer::timeout, this, [this] {
    if (run_.has_value() && host_.finish_run) host_.finish_run();
  });
  gradient_handles_ = app_settings().value(QLatin1String(kShapeAppearanceGradientHandlesKey), false).toBool();
}

ShapeAppearancePanel::~ShapeAppearancePanel() = default;

bool ShapeAppearancePanel::is_open() const {
  return window_ != nullptr && window_->isVisible();
}

QWidget* ShapeAppearancePanel::window() const {
  return window_;
}

void ShapeAppearancePanel::open() {
  if (window_ == nullptr) {
    // The settings prefix is a persisted identifier (the window's size).
    window_ = new BrushToolWindow(QStringLiteral("shapeAppearanceWindow"), QObject::tr("Shape Appearance"),
                                  QStringLiteral("shapeAppearance/window"), owner_);
    window_->set_key_target(host_.key_target);
    window_->installEventFilter(this);  // shown or hidden: the canvas handles follow
  }
  // Build before the first show, so the window opens at the page's size.
  if (!rebuild_blocked()) {
    rebuild_timer_->stop();
    rebuild();
  }
  window_->open_window();
  if (page_ != nullptr && window_->layout() != nullptr) {
    // A size restored from a smaller page still shows this one whole.
    window_->resize(window_->size().expandedTo(window_->layout()->sizeHint()));
  }
}

void ShapeAppearancePanel::hide_for_shutdown() {
  rebuild_timer_->stop();
  idle_timer_->stop();
  if (window_ != nullptr) {
    window_->hide_for_shutdown();
  }
}

bool ShapeAppearancePanel::page_targets(std::int64_t session_id, const std::vector<LayerId>& ids) const {
  return page_ != nullptr && current_.has_value() && current_->session_id == session_id && current_->ids == ids;
}

ShapeAppearanceSettings ShapeAppearancePanel::page_settings() const {
  return page_ != nullptr && body_.settings ? body_.settings() : ShapeAppearanceSettings{};
}

void ShapeAppearancePanel::restart_idle_timer() {
  idle_timer_->start();
}

void ShapeAppearancePanel::show_unavailable(const QString& reason) {
  if (window_ != nullptr) {
    window_->set_unavailable_reason(reason);
  }
}

void ShapeAppearancePanel::sync() {
  if (!is_open()) {
    return;
  }
  // Inside the page's own change, or under a dialog it opened: look again afterwards.
  if (applying_ || child_dialog_depth_ > 0) {
    sync_wanted_ = true;
    return;
  }
  const auto reason = host_.unavailable_reason ? host_.unavailable_reason() : QString();
  if (!reason.isEmpty()) {
    // Disabling is immediate (nothing may be applied from a stale page); the page itself
    // stays until there is a target again.
    show_unavailable(reason);
    return;
  }
  auto target = host_.target ? host_.target() : std::nullopt;
  if (!target.has_value()) {
    return;
  }
  // The window's own gesture on these layers: the page already shows it.
  if (run_.has_value() && run_->session_id == target->session_id && run_->ids == target->ids) {
    return;
  }
  if (page_ != nullptr && current_.has_value() && current_->same_page_as(*target)) {
    show_unavailable(QString());
    return;
  }
  schedule_rebuild();
}

void ShapeAppearancePanel::schedule_rebuild() {
  if (!rebuild_timer_->isActive()) {
    rebuild_timer_->start(0);
  }
}

bool ShapeAppearancePanel::rebuild_blocked() const {
  if (applying_ || child_dialog_depth_ > 0) {
    return true;
  }
  // A nested event loop a row started (a prompt, a manager, a combo list) has that row's
  // handler waiting on the stack: the page under it must stay.
  if (QApplication::activeModalWidget() != nullptr || QApplication::activePopupWidget() != nullptr) {
    return true;
  }
  if (window_ != nullptr) {
    const auto children = window_->findChildren<QDialog*>();
    if (std::any_of(children.begin(), children.end(), [](const QDialog* child) { return child->isVisible(); })) {
      return true;
    }
  }
  return false;
}

void ShapeAppearancePanel::flush_pending_rebuild() {
  if (rebuild_timer_->isActive() && !rebuild_blocked()) {
    rebuild_timer_->stop();
    rebuild();
  }
}

void ShapeAppearancePanel::rebuild() {
  if (window_ == nullptr) {
    return;
  }
  if (rebuild_blocked()) {
    rebuild_timer_->start(100);  // look again once the nested dialog is gone
    return;
  }
  const auto reason = host_.unavailable_reason ? host_.unavailable_reason() : QString();
  if (!reason.isEmpty()) {
    show_unavailable(reason);
    return;
  }
  auto target = host_.target ? host_.target() : std::nullopt;
  if (!target.has_value()) {
    return;
  }
  if (run_.has_value() && run_->session_id == target->session_id && run_->ids == target->ids) {
    return;
  }
  if (page_ != nullptr && current_.has_value() && current_->same_page_as(*target)) {
    show_unavailable(QString());
    return;
  }
  build_page(std::move(*target));
}

void ShapeAppearancePanel::build_page(ShapeAppearanceTarget target) {
  int scroll_position = 0;
  if (page_ != nullptr) {
    if (const auto* scroll = page_->findChild<QScrollArea*>(QStringLiteral("shapeAppearanceScroll"))) {
      scroll_position = scroll->verticalScrollBar()->value();
    }
  }
  const bool was_visible = window_->isVisible();
  window_->setUpdatesEnabled(false);

  auto* page = new ShapeAppearancePage(window_);
  page->setObjectName(QStringLiteral("shapeAppearanceBody"));
  auto options = std::move(target.options);
  target.options = {};
  target.options.units = options.units;  // the page key compares them
  options.mode = ShapeAppearanceBodyMode::Live;
  options.batch = &target.context;
  options.show_gradient_handles = gradient_handles_;
  // A page that was replaced is hidden and waiting to be deleted: nothing it still sends
  // may be applied.
  const QPointer<QWidget> guard(page);
  options.changed = [this, guard](const ShapeAppearanceSettings& settings) {
    if (guard != nullptr && guard == page_) changed(settings);
  };
  options.gradient_handles_toggled = [this, guard](bool show) {
    if (guard != nullptr && guard == page_) set_gradient_handles(show);
  };
  options.run_child_dialog = [this, guard](const std::function<bool()>& body) {
    if (guard == nullptr || guard != page_) return;
    if (host_.run_child_dialog) {
      host_.run_child_dialog(body);
    } else {
      body();
    }
  };
  body_ = build_shape_appearance_body(*page, *window_, std::move(options));
  // The page's size hint is the size that shows its rows. The window's title bar, margins
  // and size grip count against the screen too, or a page as tall as the screen would be
  // squeezed after the fact and its scroll bar would cover the right column. Measured
  // before the page joins the window: its rows' hidden-row hints are settled only until
  // it is shown.
  int chrome_height = 0;
  if (window_->layout() != nullptr) {
    window_->layout()->activate();
    chrome_height = window_->layout()->sizeHint().height();
    if (page_ != nullptr) {
      chrome_height -= page_->sizeHint().height();
    } else {
      chrome_height += 24;  // the size grip row the first page brings with it
    }
  }
  const QScreen* screen = window_->screen() != nullptr ? window_->screen() : QGuiApplication::primaryScreen();
  page->set_fitted_size(body_.fitted_size(screen, std::max(0, chrome_height)));

  // The builder titled the window for this target (Shape Appearance, or Fill Layer Settings
  // for a fill layer); the title bar the window draws itself follows.
  if (auto* title = window_->findChild<QLabel*>(QStringLiteral("dialogChromeTitleLabel"))) {
    title->setText(window_->windowTitle());
  }

  page_ = page;
  current_ = std::move(target);
  window_->replace_body(page);  // hides the old page and deletes it later
  window_->set_unavailable_reason(QString());
  if (auto* scroll = page->findChild<QScrollArea*>(QStringLiteral("shapeAppearanceScroll"))) {
    scroll->verticalScrollBar()->setValue(scroll_position);
  }
  if (was_visible && window_->layout() != nullptr) {
    // Grow to show the new page's rows; never shrink a size the user chose.
    window_->layout()->activate();
    window_->resize(window_->size().expandedTo(window_->layout()->sizeHint()));
  }
  window_->setUpdatesEnabled(true);
}

void ShapeAppearancePanel::changed(const ShapeAppearanceSettings& settings) {
  if (child_preview_) {
    child_preview_(settings);
    return;
  }
  if (child_dialog_depth_ > 0 || !host_.apply) {
    return;
  }
  // A copy: ending a run rebases the page, which replaces the edits `settings` refers to.
  const ShapeAppearanceSettings applied = settings;
  applying_ = true;
  host_.apply(applied);
  applying_ = false;
  if (sync_wanted_) {
    sync_wanted_ = false;
    sync();
  }
}

void ShapeAppearancePanel::rebase_page() {
  if (page_ == nullptr || !current_.has_value() || !body_.rebase) {
    return;
  }
  auto target = host_.unavailable_reason && host_.unavailable_reason().isEmpty() && host_.target
                    ? host_.target()
                    : std::nullopt;
  if (target.has_value() && target->same_layers_as(*current_) &&
      target->context.originals.size() == current_->context.originals.size()) {
    body_.rebase(target->context.originals);
    current_->revisions = std::move(target->revisions);
    return;
  }
  // The targets changed under the run (a layer deleted, another document): a new page.
  current_.reset();
  if (is_open()) schedule_rebuild();
}

void ShapeAppearancePanel::run_finished() {
  idle_timer_->stop();
  rebase_page();
}

bool ShapeAppearancePanel::gradient_handles_wanted() const {
  return gradient_handles_ && is_open();
}

void ShapeAppearancePanel::set_gradient_handles(bool show) {
  if (gradient_handles_ == show) {
    return;
  }
  gradient_handles_ = show;
  app_settings().setValue(QLatin1String(kShapeAppearanceGradientHandlesKey), show);
  if (host_.gradient_handles_changed) host_.gradient_handles_changed();
}

bool ShapeAppearancePanel::eventFilter(QObject* watched, QEvent* event) {
  if (watched == window_ && (event->type() == QEvent::Show || event->type() == QEvent::Hide) &&
      host_.gradient_handles_changed) {
    // After the event: the window's visibility is what the host reads.
    QTimer::singleShot(0, this, [this] {
      if (host_.gradient_handles_changed) host_.gradient_handles_changed();
    });
  }
  return QObject::eventFilter(watched, event);
}

void ShapeAppearancePanel::begin_child_dialog() {
  ++child_dialog_depth_;
}

void ShapeAppearancePanel::set_child_preview(std::function<void(const ShapeAppearanceSettings&)> preview) {
  child_preview_ = std::move(preview);
}

void ShapeAppearancePanel::end_child_dialog() {
  child_preview_ = {};
  child_dialog_depth_ = std::max(0, child_dialog_depth_ - 1);
  if (child_dialog_depth_ > 0) {
    return;
  }
  rebase_page();
  if (sync_wanted_) {
    sync_wanted_ = false;
    sync();
  }
}

}  // namespace patchy::ui
