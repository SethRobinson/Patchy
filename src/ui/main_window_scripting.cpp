// MainWindow's scripting surface, split out of main_window.cpp: the lazy
// ScriptEngineHost accessor, the File > Scripts submenu (bundled + user script
// scan), the Script Manager dialog entry, and the CLI `--run-script` flows
// (run_script_command for forwarded requests, run_cli_script for unattended
// launches). The engine itself lives in script_engine.cpp; see
// docs/scripting.md.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "ui/action_icons.hpp"
#include "ui/ai_setup_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/cli_exit.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/markdown_viewer_dialog.hpp"
#include "ui/script_editor_dialog.hpp"
#include "ui/script_engine.hpp"
#include "ui/script_folders.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/update_checker.hpp"
#include "ui/zoom_status_bar.hpp"
#include "support/string_utils.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>
#include <utility>

namespace patchy::ui {

namespace {

// Marks the submenus and separator the folder rescan rebuilds (the static entries stay;
// the script actions themselves are persistent, see refresh_script_commands).
constexpr char kDynamicScriptActionProperty[] = "patchy.scriptMenuEntry";
// Dynamic properties of a persistent script action: the hotkey command id, the absolute
// path that runs (refreshed by every scan: a shadow override swaps it), and the @hotkey
// default it was registered with (PortableText; a change re-registers the default).
constexpr char kScriptCommandIdProperty[] = "patchy.scriptCommandId";
constexpr char kScriptPathProperty[] = "patchy.scriptPath";
constexpr char kScriptHotkeyProperty[] = "patchy.scriptHotkey";
// For the right-click menu: the relative path (icon target), the shadowed bundled
// original (Revert to Bundled), and the @name display name (the Hotkeys page search).
constexpr char kScriptRelativePathProperty[] = "patchy.scriptRelativePath";
constexpr char kScriptBundledPathProperty[] = "patchy.scriptBundledPath";
constexpr char kScriptDisplayNameProperty[] = "patchy.scriptDisplayName";

// Right-clicks on the entries of `menu` open the script context menu.
void configure_scripts_context_menu(QMenu* menu, QObject* filter) {
  if (menu == nullptr || menu->property(kScriptsMenuProperty).toBool()) {
    return;
  }
  menu->setProperty(kScriptsMenuProperty, true);
  menu->setContextMenuPolicy(Qt::CustomContextMenu);
  menu->installEventFilter(filter);
}

// Runs `script_path`, captures console output and errors, and writes them (plus
// a final "[done]"/"[failed]" line) to output_path when the run fully
// completes. The output file is the CLI contract AI agents poll (the invoking
// `patchy --run-script` process exits immediately in the forwarded flow).
void run_script_writing_output(ScriptEngineHost& host, const QString& script_path,
                               const QString& output_path, const QStringList& script_args,
                               std::function<void(bool ok)> on_finished) {
  auto capture = std::make_shared<QStringList>();
  auto connections = std::make_shared<std::vector<QMetaObject::Connection>>();
  auto finalize = [capture, connections, output_path,
                   on_finished = std::move(on_finished)](bool ok) {
    for (const auto& connection : *connections) {
      QObject::disconnect(connection);
    }
    connections->clear();
    if (!output_path.isEmpty()) {
      QFile file(output_path);
      if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        for (const auto& line : *capture) {
          file.write(line.toUtf8());
          file.write("\n");
        }
        file.write(ok ? "[done]\n" : "[failed]\n");
      }
    }
    if (on_finished) {
      on_finished(ok);
    }
  };
  connections->push_back(QObject::connect(
      &host, &ScriptEngineHost::message_emitted, &host, [capture](int kind, const QString& text) {
        switch (kind) {
          case 1:
            capture->append(QStringLiteral("[warn] ") + text);
            break;
          case 2:
            capture->append(QStringLiteral("[error] ") + text);
            break;
          default:
            capture->append(text);
            break;
        }
      }));
  connections->push_back(QObject::connect(&host, &ScriptEngineHost::run_state_changed, &host,
                                          [&host, finalize] {
                                            if (!host.run_active()) {
                                              finalize(!host.last_run_had_error());
                                            }
                                          }));
  // CLI-originated (forwarded or unattended launch): interactive helpers must
  // answer with defaults even when a GUI instance executes the script.
  const bool started_clean = host.run_file(script_path, script_args, /*unattended=*/true);
  if (!host.run_active()) {
    // The run never started (unreadable file, or another script owns the
    // engine) or already finished synchronously before the completion signal
    // could be connected... which cannot happen (completion is deferred), so
    // this covers exactly the never-started case.
    finalize(started_clean);
  }
}

}  // namespace

ScriptEngineHost& MainWindow::script_engine_host() {
  if (script_engine_host_ == nullptr) {
    script_engine_host_ = new ScriptEngineHost(*this);
    // Errors always surface on the status bar (menu and CLI runs have no
    // console pane; the editor dialog additionally shows them in its own).
    connect(script_engine_host_, &ScriptEngineHost::message_emitted, this,
            [this](int kind, const QString& text) {
              if (kind == 2) {
                show_status_error(text);
              }
            });
  }
  return *script_engine_host_;
}

QString MainWindow::bundled_scripts_directory() {
  QStringList candidates;
  candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/scripts");
#ifdef Q_OS_MACOS
  candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/scripts");
#endif
#ifdef Q_OS_LINUX
  candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/../share/patchy/scripts");
#endif
  for (const auto& candidate : candidates) {
    if (QDir(candidate).exists()) {
      return QDir(candidate).absolutePath();
    }
  }
  return {};
}

QString MainWindow::user_scripts_directory() {
  // Isolation knob like PATCHY_USER_FONTS_DIR: the UI suite points every test process at
  // a scratch folder, so a developer's real scripts (and their @hotkey defaults) never
  // register in test windows.
  const auto override_dir = qEnvironmentVariable("PATCHY_USER_SCRIPTS_DIR");
  const auto path = override_dir.isEmpty()
                        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/scripts")
                        : QDir::cleanPath(override_dir);
  QDir().mkpath(path);
  return QDir(path).absolutePath();
}

void MainWindow::refresh_script_commands(const ScriptScan& scan) {
  bool changed = false;
  QSet<QString> seen;
  const std::function<void(const std::vector<ScriptFolderEntry>&)> visit =
      [this, &visit, &seen, &changed](const std::vector<ScriptFolderEntry>& entries) {
        for (const auto& entry : entries) {
          if (entry.is_folder) {
            visit(entry.children);
            continue;
          }
          const auto id = script_hotkey_command_id(entry.relative_path);
          seen.insert(id);
          QList<QKeySequence> defaults;
          if (!entry.hotkey.isEmpty()) {
            defaults << QKeySequence::fromString(entry.hotkey, QKeySequence::PortableText);
          }
          auto* action = script_actions_.value(id);
          if (action == nullptr) {
            action = new QAction(this);
            action->setMenuRole(QAction::NoRole);  // never merged on macOS (docs/platform.md)
            action->setObjectName(QStringLiteral("scriptAction.") + id);
            action->setProperty(kScriptCommandIdProperty, id);
            action->setProperty(kScriptHotkeyProperty, entry.hotkey);
            action->setProperty(kScriptRelativePathProperty, entry.relative_path);
            connect(action, &QAction::triggered, this, [this, action] {
              run_script_from_menu(action->property(kScriptPathProperty).toString());
            });
            register_hotkey(action, id, defaults, QStringLiteral("scripts"));
            // Floats associate every registered action so Qt's WindowShortcut context
            // matches while one is active (main_window_sessions.cpp); later arrivals must
            // join the same way.
            for (const auto& candidate : sessions_) {
              if (candidate != nullptr && candidate->float_window != nullptr) {
                candidate->float_window->addAction(action);
              }
            }
            script_actions_.insert(id, action);
            changed = true;
          } else if (action->property(kScriptHotkeyProperty).toString() != entry.hotkey) {
            action->setProperty(kScriptHotkeyProperty, entry.hotkey);
            hotkey_registry_.set_default_shortcuts(id, defaults);
            changed = true;
          }
          // The menu text; the Hotkeys page shows the same label under "Scripts".
          action->setText(entry.is_override ? tr("%1 (modified)").arg(entry.display_name) : entry.display_name);
          action->setIcon(script_entry_icon(entry));
          action->setProperty(kScriptPathProperty, entry.path);
          action->setProperty(kScriptBundledPathProperty, entry.bundled_path);
          action->setProperty(kScriptDisplayNameProperty, entry.display_name);
        }
      };
  visit(scan.bundled);
  visit(scan.user);
  for (auto it = script_actions_.begin(); it != script_actions_.end();) {
    if (seen.contains(it.key())) {
      ++it;
      continue;
    }
    hotkey_registry_.unregister_command(it.key());
    // ~QAction removes it from every menu and float window that lists it.
    it.value()->deleteLater();
    it = script_actions_.erase(it);
    changed = true;
  }
  if (changed) {
    hotkey_registry_.apply_to_actions();
  }
}

ScriptScan MainWindow::rescan_scripts() {
  auto scan = scan_scripts(bundled_scripts_directory(), user_scripts_directory());
  refresh_script_commands(scan);
  if (auto* dialog = qobject_cast<ScriptEditorDialog*>(script_editor_dialog_.data()); dialog != nullptr) {
    dialog->refresh_tree_keeping_selection();
  }
  return scan;
}

void MainWindow::show_script_context_menu(QMenu* menu, const QPoint& position) {
  if (menu == nullptr) {
    return;
  }
  auto* action = menu->actionAt(position);
  if (action == nullptr || !action->property(kScriptCommandIdProperty).isValid()) {
    return;  // folder submenus, separators, the static entries
  }
  const auto path = action->property(kScriptPathProperty).toString();
  const auto relative_path = action->property(kScriptRelativePathProperty).toString();
  const auto bundled_path = action->property(kScriptBundledPathProperty).toString();
  const auto display_name = action->property(kScriptDisplayNameProperty).toString();
  // The chosen command replaces the menu trip, so the whole File > Scripts chain closes.
  const auto close_menus = [this, menu] {
    menu->close();
    if (scripts_menu_ != nullptr) {
      scripts_menu_->close();
      if (auto* file_menu = qobject_cast<QMenu*>(scripts_menu_->parentWidget()); file_menu != nullptr) {
        file_menu->close();
      }
    }
  };

  // Each entry carries its own handler (the recent-files pattern): a context menu
  // action triggered programmatically, which is how tests drive it, never comes back
  // from exec() on every platform.
  QMenu context_menu(this);
  context_menu.setObjectName(QStringLiteral("scriptMenuContextMenu"));
  auto* run_action = context_menu.addAction(tr("Run"));
  run_action->setObjectName(QStringLiteral("scriptMenuRunAction"));
  connect(run_action, &QAction::triggered, this, [this, close_menus, path] {
    close_menus();
    run_script_from_menu(path);
  });
  auto* edit_action = context_menu.addAction(tr("Edit in Script Manager..."));
  edit_action->setObjectName(QStringLiteral("scriptMenuEditAction"));
  connect(edit_action, &QAction::triggered, this, [this, close_menus, path] {
    close_menus();
    open_script_editor();
    if (auto* dialog = qobject_cast<ScriptEditorDialog*>(script_editor_dialog_.data()); dialog != nullptr) {
      dialog->open_script(path);
    }
  });
  auto* reveal_action = context_menu.addAction(tr("Show in Folder"));
  reveal_action->setObjectName(QStringLiteral("scriptMenuRevealAction"));
  connect(reveal_action, &QAction::triggered, this, [this, close_menus, path] {
    close_menus();
    reveal_path_in_file_explorer(path, /*is_file=*/true);
  });
  auto* cli_action = context_menu.addAction(tr("Command Line Example..."));
  cli_action->setObjectName(QStringLiteral("scriptMenuCliAction"));
  connect(cli_action, &QAction::triggered, this, [this, close_menus, path] {
    close_menus();
    ScriptEditorDialog::show_cli_example_dialog(this, path);
  });
  auto* hotkey_action = context_menu.addAction(tr("Assign Hotkey..."));
  hotkey_action->setObjectName(QStringLiteral("scriptMenuHotkeyAction"));
  connect(hotkey_action, &QAction::triggered, this, [this, close_menus, display_name] {
    close_menus();
    show_hotkey_preferences(display_name);
  });
  auto* icon_action = context_menu.addAction(tr("Set Icon from Current Window"));
  icon_action->setObjectName(QStringLiteral("scriptMenuIconAction"));
  connect(icon_action, &QAction::triggered, this, [this, close_menus, relative_path] {
    close_menus();
    QString target;
    const auto error = ScriptEditorDialog::write_icon_from_current_window(script_engine_host(), relative_path, &target);
    if (!error.isEmpty()) {
      show_status_error(error);
      return;
    }
    statusBar()->showMessage(tr("Saved icon to %1").arg(QDir::toNativeSeparators(target)));
    rescan_scripts();
  });
  if (!bundled_path.isEmpty()) {
    context_menu.addSeparator();
    auto* revert_action = context_menu.addAction(tr("Revert to Bundled"));
    revert_action->setObjectName(QStringLiteral("scriptMenuRevertAction"));
    connect(revert_action, &QAction::triggered, this, [this, close_menus, path, bundled_path] {
      close_menus();
      QString error;
      if (ScriptEditorDialog::confirm_and_revert_override(this, path, bundled_path, &error)) {
        rescan_scripts();
      } else if (!error.isEmpty()) {
        show_status_error(error);
      }
    });
  }
  context_menu.exec(menu->mapToGlobal(position));
}

void MainWindow::rebuild_scripts_menu() {
  if (scripts_menu_ == nullptr) {
    return;
  }
  const auto scan = scan_scripts(bundled_scripts_directory(), user_scripts_directory());
  refresh_script_commands(scan);
  configure_scripts_context_menu(scripts_menu_, this);  // top-level user scripts
  const auto actions = scripts_menu_->actions();
  for (auto* action : actions) {
    if (action->property(kDynamicScriptActionProperty).toBool()) {
      scripts_menu_->removeAction(action);
      if (auto* submenu = action->menu()) {
        submenu->deleteLater();  // owns its menuAction, never the script actions listed in it
      } else {
        action->deleteLater();  // the bundled/user separator
      }
    } else if (action->property(kScriptCommandIdProperty).isValid()) {
      scripts_menu_->removeAction(action);  // persistent: re-added below in scan order
    }
  }
  // Folders become submenus; a user shadow copy replaces the bundled entry in
  // place, tagged "(modified)" (script_folders.hpp). Entries are the persistent
  // script actions (refresh_script_commands keeps their @name display name,
  // sidecar icon and shortcut current), so the menu shows each one's hotkey.
  const std::function<void(QMenu*, const std::vector<ScriptFolderEntry>&, bool)> add_entries =
      [this, &add_entries](QMenu* menu, const std::vector<ScriptFolderEntry>& entries, bool mark) {
        for (const auto& entry : entries) {
          if (entry.is_folder) {
            auto* submenu = menu->addMenu(script_folder_display_name(entry.name));
            submenu->menuAction()->setMenuRole(QAction::NoRole);  // submenus never merge on macOS (docs/platform.md)
            configure_scripts_context_menu(submenu, this);
            if (mark) {
              submenu->menuAction()->setProperty(kDynamicScriptActionProperty, true);
            }
            add_entries(submenu, entry.children, false);
            continue;
          }
          if (auto* action = script_actions_.value(script_hotkey_command_id(entry.relative_path)); action != nullptr) {
            menu->addAction(action);
          }
        }
      };
  add_entries(scripts_menu_, scan.bundled, true);
  if (!scan.user.empty()) {
    auto* separator = scripts_menu_->addSeparator();
    separator->setProperty(kDynamicScriptActionProperty, true);
    add_entries(scripts_menu_, scan.user, true);
  }
}

void MainWindow::run_script_from_menu(const QString& path) {
  auto& host = script_engine_host();
  if (host.run_active()) {
    show_status_error(tr("A script is already running: %1").arg(host.active_run_name()));
    return;
  }
  statusBar()->showMessage(tr("Running script %1...").arg(QFileInfo(path).fileName()));
  (void)host.run_file(path);
}

void MainWindow::browse_user_scripts_folder() {
  QDesktopServices::openUrl(QUrl::fromLocalFile(user_scripts_directory()));
}

void MainWindow::open_scripting_guide() {
  if (scripting_guide_dialog_ != nullptr) {
    scripting_guide_dialog_->show();
    scripting_guide_dialog_->raise();
    scripting_guide_dialog_->activateWindow();
    return;
  }
  const auto bundled = bundled_scripts_directory();
  const auto path =
      bundled.isEmpty() ? QString() : bundled + QStringLiteral("/scripting-guide.md");
  auto* dialog = new MarkdownViewerDialog(this);
  dialog->setWindowTitle(tr("Scripting Guide"));
  if (!dialog->load_file(path)) {
    delete dialog;
    show_status_error(tr("The scripting guide (scripting-guide.md) is missing from the bundled "
                         "scripts folder."));
    return;
  }
  // Parented to the main window (not the Script Manager) so the guide
  // survives closing the manager and both Help entries share one instance.
  scripting_guide_dialog_ = dialog;
  run_non_modal_dialog(*dialog);
}

void MainWindow::open_ai_setup_dialog() {
  if (ai_setup_dialog_ != nullptr) {
    ai_setup_dialog_->show();
    ai_setup_dialog_->raise();
    ai_setup_dialog_->activateWindow();
    return;
  }
  // Always opens, even on a build without the connector or the assembled skill:
  // the text then says NOT FOUND and the online guide button is the way out.
  auto* dialog = new AiSetupDialog(resolve_ai_control_paths(), this);
  connect(dialog, &AiSetupDialog::blurb_copied, this,
          [this] { statusBar()->showMessage(tr("AI setup text copied to the clipboard")); });
  ai_setup_dialog_ = dialog;
  run_non_modal_dialog(*dialog);
}

void MainWindow::open_script_editor() {
  if (script_editor_dialog_ != nullptr) {
    script_editor_dialog_->show();
    script_editor_dialog_->raise();
    script_editor_dialog_->activateWindow();
    return;
  }
  auto* dialog = new ScriptEditorDialog(*this, script_engine_host());
  script_editor_dialog_ = dialog;
  run_non_modal_dialog(*dialog);
}

void MainWindow::run_script_command(const QString& script_path, const QString& output_path,
                                    const QStringList& script_args) {
  auto& host = script_engine_host();
  if (host.run_active()) {
    if (!output_path.isEmpty()) {
      QFile file(output_path);
      if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        file.write("[error] A script is already running.\n[failed]\n");
      }
    }
    return;
  }
  run_script_writing_output(host, script_path, output_path, script_args, {});
}

void MainWindow::run_cli_script(const QString& script_path, const QString& output_path,
                                const QStringList& script_args) {
  // Deferred like run_cli_export: the run starts once the event loop is up, so
  // command-line file opens have fully settled into sessions first.
  QTimer::singleShot(0, this, [this, script_path, output_path, script_args] {
    auto& host = script_engine_host();
    run_script_writing_output(host, script_path, output_path, script_args, [this](bool ok) {
      // No document may prompt during shutdown.
      for (auto& session : sessions_) {
        if (session != nullptr) {
          set_session_saved(*session);
        }
      }
      exit_cli_application(ok ? 0 : 4);
    });
  });
}

}  // namespace patchy::ui
