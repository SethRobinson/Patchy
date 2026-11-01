// MainWindow's GIMP 3 plug-in support (docs/gimp-plugins.md): the scan of the user's
// GIMP install on a worker, the Plugins > GIMP 3 Plug-ins menu built from what the
// plug-ins register, and running one on the active layer through patchy-gimp-host, with
// the plug-in's own dialog, live canvas updates, the selection and one undo step.

#include "ui/main_window.hpp"

#include "color/rgb_profiles.hpp"
#include "core/layer.hpp"
#include "core/pixel_buffer.hpp"
#include "core/vector_shape.hpp"
#include "support/startup_trace.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/command_search.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/gimp_plugin_catalog.hpp"
#include "ui/gimp_plugin_run_dialog.hpp"
#include "ui/gimp_plugin_runner.hpp"
#include "ui/localization.hpp"
#include "ui/main_window_shared.hpp"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <map>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace patchy::ui {

namespace {

constexpr char kGimpPluginMenuEntryProperty[] = "patchy.gimpPluginMenuEntry";

QString sentence_name(const QString& label) { return gimp_plugin_display_name(label); }

QString plugin_language() { return gimp_plugin_language(); }

}  // namespace

GimpPluginScanOptions MainWindow::gimp_plugin_scan_options() const {
  GimpPluginScanOptions options;
  options.enabled = stored_gimp_plugins_enabled();
  options.install_folder = stored_gimp_install_folder();
  options.extra_folders = stored_gimp_plugin_folders();
  options.language = plugin_language();
  options.only_extra_folders = qEnvironmentVariableIntValue("PATCHY_GIMP_SCAN_ONLY_ADDED") != 0;
  return options;
}

void MainWindow::rescan_gimp_plugins(bool force_requery) {
  ++gimp_plugin_scan_generation_;
  gimp_plugin_scan_in_flight_ = false;
  gimp_plugin_scan_pending_ = false;
  auto options = gimp_plugin_scan_options();
  options.force_requery = force_requery;
  gimp_plugins_ = std::make_shared<GimpPluginScanResult>(scan_gimp_plugins(options));
  rebuild_gimp_plugins_menu();
}

void MainWindow::start_gimp_plugin_scan(bool announce, bool force_requery) {
  if (gimp_plugin_scan_in_flight_) {
    gimp_plugin_scan_pending_ = true;
    return;
  }
  gimp_plugin_scan_in_flight_ = true;
  const int generation = ++gimp_plugin_scan_generation_;
  rebuild_gimp_plugins_menu();  // the "Scanning..." note while nothing is listed
  auto options = gimp_plugin_scan_options();
  options.force_requery = force_requery;
  auto* app = QApplication::instance();
  QPointer<MainWindow> window(this);
  const double trace_start = startup_trace::enabled() ? startup_trace::now_ms() : -1.0;
  run_tracked_background_worker([app, window, options, generation, announce, trace_start] {
    auto result = std::make_shared<GimpPluginScanResult>(scan_gimp_plugins(options));
    if (app == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(
        app,
        [window, result, generation, announce, trace_start] {
          if (window != nullptr) {
            window->finish_gimp_plugin_scan(generation, result, announce);
            startup_trace::record_background("plugins.gimp_scan", trace_start, startup_trace::now_ms());
          }
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::finish_gimp_plugin_scan(int generation, std::shared_ptr<GimpPluginScanResult> result, bool announce) {
  if (generation != gimp_plugin_scan_generation_) {
    return;
  }
  gimp_plugin_scan_in_flight_ = false;
  gimp_plugins_ = std::move(result);
  if (QApplication::activePopupWidget() == nullptr) {
    rebuild_gimp_plugins_menu();
  } else {
    QTimer::singleShot(1000, this, [this] {
      if (QApplication::activePopupWidget() == nullptr) {
        rebuild_gimp_plugins_menu();
      }
    });
  }
  if (announce) {
    int available = 0;
    int unsupported = 0;
    for (const auto& p : gimp_plugins_->procedures) {
      available += p.use == QLatin1String("layer") ? 1 : 0;
      unsupported += p.use == QLatin1String("unsupported") ? 1 : 0;
    }
    statusBar()->showMessage(!gimp_plugins_->problem.isEmpty()
                                 ? gimp_plugins_->problem
                                 //: Status bar after Plugins > GIMP 3 Plug-ins > Rescan.
                                 : tr("GIMP plug-in scan finished: %1 available, %2 not usable").arg(available).arg(unsupported));
  }
  if (gimp_plugin_scan_pending_) {
    gimp_plugin_scan_pending_ = false;
    start_gimp_plugin_scan(false);
  }
}

void MainWindow::ensure_gimp_plugins_scanned() {
  // Offscreen runs (tests, --headless) skip the startup scan; the first use scans here.
  if (gimp_plugins_ == nullptr) {
    rescan_gimp_plugins(false);
  }
}

const GimpPluginProcedure* MainWindow::find_gimp_plugin(const QString& identifier) const noexcept {
  if (gimp_plugins_ == nullptr) {
    return nullptr;
  }
  for (const auto& p : gimp_plugins_->procedures) {
    if (p.identifier == identifier) {
      return &p;
    }
  }
  return nullptr;
}

QString MainWindow::gimp_plugin_reason_text(const QString& code) {
  if (code == QLatin1String("not-image-procedure")) {
    return tr("It does not work on an image layer.");
  }
  if (code == QLatin1String("needs-no-layer")) {
    return tr("It makes something new instead of changing a layer.");
  }
  if (code == QLatin1String("needs-several-layers")) {
    return tr("It needs several layers at once.");
  }
  if (code == QLatin1String("not-rgb")) {
    return tr("It works only on grayscale or indexed images.");
  }
  if (code == QLatin1String("menu-outside-filters")) {
    return tr("It changes the image itself, not the pixels of one layer.");
  }
  if (code == QLatin1String("script-fu")) {
    return tr("Script-Fu scripts are built from GIMP's own image operations, which Patchy does not provide.");
  }
  if (code == QLatin1String("creates-image")) {
    return tr("It creates a new image, which Patchy does not support for GIMP plug-ins yet.");
  }
  if (code == QLatin1String("animation")) {
    return tr("It works on the layers of an animation, which Patchy does not hand to GIMP plug-ins.");
  }
  if (code == QLatin1String("image-operations")) {
    return tr("It builds its result with GIMP's own image operations (new layers or images, the clipboard, painting "
              "tools), which Patchy does not provide.");
  }
  if (code == QLatin1String("uses-gimp-resources")) {
    return tr("It needs GIMP's brushes, patterns, gradients, fonts or palettes, which Patchy does not provide yet.");
  }
  if (code == QLatin1String("file-handler") || code == QLatin1String("no-menu") || code == QLatin1String("temporary")) {
    return tr("It is a file format or a helper for other plug-ins.");
  }
  return tr("Patchy cannot run it.");
}

QString MainWindow::gimp_plugin_error_text(const QString& name, const GimpPluginRunResult& result) {
  const auto& code = result.code;
  QString text;
  if (code == QLatin1String("unsupported-procedure") &&
      (result.detail == QLatin1String("gimp-layer-add-alpha") ||
       result.unsupported_calls.contains(QStringLiteral("gimp-layer-add-alpha")))) {
    //: %1 is the plug-in's name.
    text = tr("%1 needs a layer that can hold transparency. Right-click the Background in the Layers panel and "
              "choose Layer from Background, then run it again. The layer was not changed.")
               .arg(name);
  } else if (code == QLatin1String("unsupported-procedure")) {
    //: %1 is the plug-in's name, %2 the GIMP procedure it called (for example gimp-layer-new).
    text = tr("%1 needs a GIMP feature Patchy does not support yet (%2). The layer was not changed.")
               .arg(name, result.detail.isEmpty() ? result.unsupported_calls.value(0) : result.detail);
  } else if (code == QLatin1String("crashed") || code == QLatin1String("protocol") || code == QLatin1String("quit")) {
    text = tr("%1 stopped unexpectedly. The layer was not changed.").arg(name);
  } else if (code == QLatin1String("timeout")) {
    text = tr("%1 did not finish in time and was stopped. The layer was not changed.").arg(name);
  } else if (code == QLatin1String("start") || code == QLatin1String("helper-crashed") || code == QLatin1String("link")) {
    text = tr("%1 could not be started. The layer was not changed.").arg(name);
  } else if (code == QLatin1String("unsupported-argument")) {
    text = tr("%1 takes a kind of setting Patchy cannot fill in yet. The layer was not changed.").arg(name);
  } else {
    text = tr("%1 reported an error. The layer was not changed.").arg(name);
  }
  QStringList details;
  for (const auto& m : result.messages) {
    details << m;
  }
  if (!result.message.trimmed().isEmpty()) {
    details << result.message.trimmed();
  }
  if (!details.isEmpty()) {
    text += QStringLiteral("\n\n") + details.join(QLatin1Char('\n')).left(1500);
  }
  return text;
}

void MainWindow::rebuild_gimp_plugins_menu() {
  if (gimp_plugins_menu_ == nullptr) {
    return;
  }
  for (auto* action : gimp_plugins_menu_->actions()) {
    if (!action->property(kGimpPluginMenuEntryProperty).toBool()) {
      continue;
    }
    gimp_plugins_menu_->removeAction(action);
    if (auto* submenu = action->menu()) {
      // Submenus nest; unregister every action below this one.
      std::vector<QMenu*> stack{submenu};
      while (!stack.empty()) {
        auto* menu = stack.back();
        stack.pop_back();
        for (auto* child : menu->actions()) {
          if (child->menu() != nullptr) {
            stack.push_back(child->menu());
          } else {
            unregister_document_action(child);
          }
        }
      }
      submenu->deleteLater();
    } else {
      unregister_document_action(action);
      action->deleteLater();
    }
  }
  // Dynamic entries go above the static commands (Rescan) at the bottom.
  QAction* before = gimp_plugins_menu_->actions().value(0);
  const auto add_note = [&](const QString& text, const char* object_name) {
    auto* note = new QAction(text, gimp_plugins_menu_);
    note->setObjectName(QString::fromLatin1(object_name));
    note->setEnabled(false);
    note->setProperty(kGimpPluginMenuEntryProperty, true);
    gimp_plugins_menu_->insertAction(before, note);
  };
  std::vector<const GimpPluginProcedure*> runnable;
  if (gimp_plugins_ != nullptr) {
    for (const auto& p : gimp_plugins_->procedures) {
      if (p.use == QLatin1String("layer")) {
        runnable.push_back(&p);
      }
    }
  }
  if (runnable.empty()) {
    if (gimp_plugin_scan_in_flight_) {
      add_note(tr("Scanning GIMP plug-ins..."), "gimpPluginsScanningNote");
    } else if (gimp_plugins_ != nullptr && !gimp_plugins_->problem.isEmpty()) {
      add_note(gimp_plugins_->problem, "gimpPluginsProblemNote");
    } else {
      add_note(tr("No GIMP 3 plug-ins found"), "gimpPluginsEmptyNote");
    }
  } else {
    std::sort(runnable.begin(), runnable.end(), [](const auto* a, const auto* b) {
      const auto menu = a->menu.join(QLatin1Char('/')).compare(b->menu.join(QLatin1Char('/')), Qt::CaseInsensitive);
      return menu != 0 ? menu < 0 : a->label.compare(b->label, Qt::CaseInsensitive) < 0;
    });
    std::map<QString, QMenu*> menus;  // by joined path
    const std::function<QMenu*(const QStringList&)> menu_for = [&](const QStringList& path) -> QMenu* {
      if (path.isEmpty()) {
        return nullptr;
      }
      const auto key = path.join(QLatin1Char('/'));
      if (auto it = menus.find(key); it != menus.end()) {
        return it->second;
      }
      auto* parent = menu_for(path.mid(0, path.size() - 1));
      QMenu* menu = nullptr;
      if (parent == nullptr) {
        menu = new QMenu(escape_qaction_ampersands(path.last()), gimp_plugins_menu_);
        menu->menuAction()->setProperty(kGimpPluginMenuEntryProperty, true);
        gimp_plugins_menu_->insertMenu(before, menu);
      } else {
        menu = parent->addMenu(escape_qaction_ampersands(path.last()));
      }
      menu->setObjectName(QStringLiteral("gimpPluginCategoryMenu"));
      menu->menuAction()->setMenuRole(QAction::NoRole);  // submenus never merge on macOS (docs/platform.md)
      menus.emplace(key, menu);
      return menu;
    };
    for (const auto* entry : runnable) {
      auto* target = menu_for(entry->menu);
      auto* action = new QAction(escape_qaction_ampersands(entry->label), target != nullptr ? static_cast<QObject*>(target)
                                                                                            : gimp_plugins_menu_);
      action->setData(entry->identifier);
      action->setObjectName(QStringLiteral("gimpPluginAction"));
      action->setProperty(kGimpPluginMenuEntryProperty, true);
      action->setProperty(kCommandSearchDataEntryProperty, true);
      action->setProperty("patchy.channelViewBlocked", true);
      action->setStatusTip(entry->blurb.isEmpty()
                               ? tr("Run the %1 GIMP plug-in on the active layer").arg(sentence_name(entry->label))
                               : entry->blurb);
      action->setMenuRole(QAction::NoRole);
      const auto identifier = entry->identifier;
      // Queued: the run must not start inside the menu's mouse handling (main_window.hpp).
      connect(action, &QAction::triggered, this, [this, identifier] {
        QTimer::singleShot(0, this, [this, identifier] { run_gimp_plugin(identifier); });
      });
      if (target != nullptr) {
        target->addAction(action);
      } else {
        gimp_plugins_menu_->insertAction(before, action);
      }
      refresh_action_tooltip(action);
      register_document_action(action);
    }
  }
  if (before != nullptr) {
    auto* separator = new QAction(gimp_plugins_menu_);
    separator->setSeparator(true);
    separator->setProperty(kGimpPluginMenuEntryProperty, true);
    gimp_plugins_menu_->insertAction(before, separator);
  }
  update_document_action_state();
}

}  // namespace patchy::ui
