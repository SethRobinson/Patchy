// MainWindow's GEGL filters (docs/gegl-filters.md): the scan of the user's GIMP 3 GEGL on
// a worker, and Plugins > GEGL Filters built from it. Runs go through apply_filter with
// the GEGL registry (main_window_filters.cpp).

#include "ui/main_window.hpp"

#include "ui/background_workers.hpp"
#include "ui/command_search.hpp"
#include "ui/gegl_filters.hpp"
#include "ui/gimp_plugin_catalog.hpp"
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

// How to start the helper for the GIMP 3 install the GIMP plug-in settings point at.
std::optional<GeglHelperCommand> gegl_command_for(const GimpPluginScanOptions& options, QString* problem) {
  const auto install = select_gimp_install(options);
  if (!install.has_value()) {
    *problem = MainWindow::tr("GEGL filters come from GIMP 3, which was not found.");
    return std::nullopt;
  }
  const auto library = find_gegl_library(*install);
  if (library.isEmpty()) {
    *problem = install->flatpak ? MainWindow::tr("GEGL filters are not available from GIMP's Flatpak yet.")
                                : MainWindow::tr("This GIMP 3 install has no GEGL library Patchy can use.");
    return std::nullopt;
  }
  return gegl_helper_command(*install, library);
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

// Patchy's own GEGL when the build ships one, whatever the GIMP settings; otherwise GIMP 3's,
// which follows the GIMP plug-in settings.
std::optional<GeglHelperCommand> scan_command(const GimpPluginScanOptions& options, QString* problem) {
  if (const auto bundled = bundled_gegl_library(); !bundled.isEmpty()) {
    return bundled_gegl_command(bundled);
  }
  if (!options.enabled) {
    *problem = MainWindow::tr("GEGL filters come from GIMP 3, which is turned off in Preferences > Plug-ins.");
    return std::nullopt;
  }
  return gegl_command_for(options, problem);
}

}  // namespace

void MainWindow::start_gegl_filter_scan() {
  QString problem;
  const auto options = gimp_plugin_scan_options();
  const auto command = scan_command(options, &problem);
  const int generation = ++gegl_filter_scan_generation_;
  gegl_filter_scan_in_flight_ = true;
  rebuild_gegl_filters_menu_when_closed();  // the "Scanning..." note
  auto* app = QApplication::instance();
  QPointer<MainWindow> window(this);
  const auto language = gimp_plugin_language();  // GEGL's labels follow Patchy's language
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
  const auto command = scan_command(gimp_plugin_scan_options(), &problem);
  finish_gegl_filter_scan(++gegl_filter_scan_generation_, scan_for(command, problem, gimp_plugin_language()));
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

void MainWindow::rebuild_gegl_filters_menu_when_closed() {
  // As for the GIMP menu: a rebuild deletes submenus, so never under an open popup.
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
  const auto* registry = gegl_filters_ != nullptr ? gegl_filters_->registry.get() : nullptr;
  if (registry == nullptr || registry->filters().empty()) {
    if (gegl_filter_scan_in_flight_ || gegl_filters_ == nullptr) {
      add_note(tr("Scanning GEGL filters..."), "geglFiltersScanningNote");
    } else {
      add_note(gegl_filters_->problem.isEmpty() ? tr("No GEGL filters found") : gegl_filters_->problem,
               "geglFiltersProblemNote");
    }
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
      // Queued, like the GIMP entries: the dialog must not open inside the menu's click.
      connect(action, &QAction::triggered, this, [this, identifier] {
        QTimer::singleShot(0, this, [this, identifier] { apply_filter(identifier); });
      });
      register_document_action(action);
    }
  }
  update_document_action_state();
}

}  // namespace patchy::ui
