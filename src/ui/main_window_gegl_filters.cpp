// MainWindow's GEGL filters (docs/gegl-filters.md): the scan of Patchy's own GEGL on a
// worker, and Filter > GEGL Filters built from it. Runs go through apply_filter with
// the GEGL registry (main_window_filters.cpp).

#include "ui/main_window.hpp"

#include "ui/background_workers.hpp"
#include "ui/command_search.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gegl_filters.hpp"
#include "ui/localization.hpp"
#include "ui/main_window_shared.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr char kGeglMenuEntryProperty[] = "patchy.geglMenuEntry";

// Patchy's own GEGL, or nothing when the build has none.
std::optional<GeglHelperCommand> scan_command(QString* problem) {
  if (const auto bundled = bundled_gegl_library(); !bundled.isEmpty()) {
    return bundled_gegl_command(bundled);
  }
  *problem = MainWindow::tr("This build of Patchy has no GEGL filters.");
  return std::nullopt;
}

std::shared_ptr<GeglFilterScanResult> scan_for(const std::optional<GeglHelperCommand>& command, const QString& problem,
                                               const QString& language) {
  if (!command.has_value()) {
    auto result = std::make_shared<GeglFilterScanResult>();
    result->problem = problem;
    return result;
  }
  return std::make_shared<GeglFilterScanResult>(scan_gegl_filters(*command, language));
}

// GEGL's labels follow Patchy's language.
QString gegl_language() {
  const auto code = LocalizationManager::instance().current_language();
  return code.isEmpty() ? QStringLiteral("en") : code;
}

}  // namespace

void MainWindow::start_gegl_filter_scan() {
  QString problem;
  const auto command = scan_command(&problem);
  const int generation = ++gegl_filter_scan_generation_;
  gegl_filter_scan_in_flight_ = true;
  rebuild_gegl_filters_menu_when_closed();  // the "Scanning..." note
  auto* app = QApplication::instance();
  QPointer<MainWindow> window(this);
  const auto language = gegl_language();
  run_tracked_background_worker([app, window, generation, command, problem, language] {
    auto result = scan_for(command, problem, language);
    if (app == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(
        app,
        [window, generation, result] {
          if (window != nullptr) {
            window->finish_gegl_filter_scan(generation, result);
          }
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::ensure_gegl_filters_scanned() {
  if (gegl_filters_ != nullptr) {
    return;
  }
  QString problem;
  const auto command = scan_command(&problem);
  finish_gegl_filter_scan(++gegl_filter_scan_generation_, scan_for(command, problem, gegl_language()));
}

void MainWindow::finish_gegl_filter_scan(int generation, std::shared_ptr<GeglFilterScanResult> result) {
  if (generation != gegl_filter_scan_generation_) {
    return;
  }
  gegl_filter_scan_in_flight_ = false;
  gegl_filters_ = std::move(result);
  rebuild_gegl_filters_menu_when_closed();
}

std::shared_ptr<const FilterRegistry> MainWindow::gegl_filter_registry_for(const QString& identifier) const {
  if (gegl_filters_ == nullptr || !identifier.startsWith(QLatin1String(kGeglFilterIdPrefix))) {
    return nullptr;
  }
  return gegl_filters_->registry;
}

void MainWindow::add_gegl_filter_credit(const QString& identifier, FilterDialogSpec& spec) const {
  if (gegl_filters_ == nullptr || !identifier.startsWith(QLatin1String(kGeglFilterIdPrefix))) {
    return;
  }
  spec.credit = gegl_filter_credit(*gegl_filters_, identifier.toStdString());
  spec.about_text = tr("About GEGL...");
  // The scan result the dialog opened with, held while the dialog lives.
  spec.show_about = [scan = std::shared_ptr<const GeglFilterScanResult>(gegl_filters_)](QWidget* parent) {
    show_about_gegl(parent, scan.get());
  };
}

void MainWindow::rebuild_gegl_filters_menu_when_closed() {
  // A rebuild deletes submenus, so never under an open popup.
  if (QApplication::activePopupWidget() == nullptr) {
    rebuild_gegl_filters_menu();
    return;
  }
  if (std::exchange(gegl_filters_menu_stale_, true)) {
    return;
  }
  QTimer::singleShot(500, this, [this] {
    if (std::exchange(gegl_filters_menu_stale_, false)) {
      rebuild_gegl_filters_menu_when_closed();
    }
  });
}

void MainWindow::rebuild_gegl_filters_menu() {
  gegl_filters_menu_stale_ = false;
  if (gegl_filters_menu_ == nullptr) {
    return;
  }
  for (auto* action : gegl_filters_menu_->actions()) {
    gegl_filters_menu_->removeAction(action);
    if (auto* submenu = action->menu()) {
      for (auto* child : submenu->actions()) {
        unregister_document_action(child);
      }
      submenu->deleteLater();
    } else {
      action->deleteLater();
    }
  }
  const auto add_note = [this](const QString& text, const char* object_name) {
    auto* note = gegl_filters_menu_->addAction(text);
    note->setObjectName(QString::fromLatin1(object_name));
    note->setEnabled(false);
    note->setProperty(kGeglMenuEntryProperty, true);
    note->setProperty(kCommandSearchDataEntryProperty, true);  // a scan state, not a command
    note->setToolTip(text);
  };
  // Last in the menu, whatever the scan found: who makes GEGL, its licenses and source.
  const auto add_about = [this] {
    gegl_filters_menu_->addSeparator()->setProperty(kGeglMenuEntryProperty, true);
    auto* about = gegl_filters_menu_->addAction(tr("About GEGL..."));
    about->setObjectName(QStringLiteral("geglAboutAction"));
    about->setMenuRole(QAction::NoRole);
    about->setProperty(kGeglMenuEntryProperty, true);
    about->setProperty(kCommandSearchDataEntryProperty, true);
    connect(about, &QAction::triggered, this, [this] {
      QTimer::singleShot(0, this, [this] { show_about_gegl(this, gegl_filters_.get()); });
    });
  };
  const auto* registry = gegl_filters_ != nullptr ? gegl_filters_->registry.get() : nullptr;
  if (registry == nullptr || registry->filters().empty()) {
    if (gegl_filter_scan_in_flight_ || gegl_filters_ == nullptr) {
      add_note(tr("Scanning GEGL filters..."), "geglFiltersScanningNote");
    } else {
      add_note(gegl_filters_->problem.isEmpty() ? tr("No GEGL filters found") : gegl_filters_->problem,
               "geglFiltersProblemNote");
    }
    add_about();
    update_document_action_state();
    return;
  }
  // One submenu per GEGL category, both sorted by name.
  std::map<QString, std::vector<const FilterDefinition*>> by_category;
  for (const auto& filter : registry->filters()) {
    const auto category = gegl_filters_->categories[filter.identifier];
    by_category[gegl_category_label(category)].push_back(&filter);
  }
  for (auto& [category, filters] : by_category) {
    std::sort(filters.begin(), filters.end(), [](const auto* a, const auto* b) {
      return QString::fromStdString(a->display_name).compare(QString::fromStdString(b->display_name),
                                                             Qt::CaseInsensitive) < 0;
    });
    auto* submenu = gegl_filters_menu_->addMenu(escape_qaction_ampersands(category));
    submenu->setObjectName(QStringLiteral("geglCategoryMenu"));
    submenu->setToolTipsVisible(true);
    submenu->menuAction()->setMenuRole(QAction::NoRole);  // submenus never merge on macOS (docs/platform.md)
    submenu->menuAction()->setProperty(kGeglMenuEntryProperty, true);
    for (const auto* filter : filters) {
      const auto identifier = QString::fromStdString(filter->identifier);
      auto* action = submenu->addAction(escape_qaction_ampersands(QString::fromStdString(filter->display_name)) +
                                        QStringLiteral("..."));
      action->setObjectName(QStringLiteral("geglFilterAction"));
      action->setData(identifier);
      action->setMenuRole(QAction::NoRole);
      action->setProperty(kCommandSearchDataEntryProperty, true);
      action->setProperty("patchy.channelViewBlocked", true);
      const auto description = gegl_filters_->descriptions[filter->identifier];
      action->setStatusTip(description);
      action->setToolTip(description);
      // Queued: the dialog must not open inside the menu's click.
      connect(action, &QAction::triggered, this, [this, identifier] {
        QTimer::singleShot(0, this, [this, identifier] { apply_filter(identifier); });
      });
      register_document_action(action);
    }
  }
  add_about();
  update_document_action_state();
}

}  // namespace patchy::ui
