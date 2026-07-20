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
#include "ui/app_settings.hpp"
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

// Marks menu entries the folder rescan rebuilds (the static entries stay).
constexpr char kDynamicScriptActionProperty[] = "patchy.scriptMenuEntry";

// Runs `script_path`, captures console output and errors, and writes them (plus
// a final "[done]"/"[failed]" line) to output_path when the run fully
// completes. The output file is the CLI contract AI agents poll (the invoking
// `patchy --run-script` process exits immediately in the forwarded flow).
void run_script_writing_output(ScriptEngineHost& host, const QString& script_path,
                               const QString& output_path, const QStringList& script_args,
                               std::function<void(bool ok)> on_finished) {
  auto capture = std::make_shared<QStringList>();
  auto connections = std::make_shared<std::vector<QMetaObject::Connection>>();
  auto finalize = [&host, capture, connections, output_path,
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
  const bool started_clean = host.run_file(script_path, script_args);
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
  const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const auto path = base + QStringLiteral("/scripts");
  QDir().mkpath(path);
  return QDir(path).absolutePath();
}

void MainWindow::rebuild_scripts_menu() {
  if (scripts_menu_ == nullptr) {
    return;
  }
  const auto actions = scripts_menu_->actions();
  for (auto* action : actions) {
    if (action->property(kDynamicScriptActionProperty).toBool()) {
      scripts_menu_->removeAction(action);
      if (auto* submenu = action->menu()) {
        submenu->deleteLater();  // owns its menuAction
      } else {
        action->deleteLater();
      }
    }
  }
  // Folders become submenus; a user shadow copy replaces the bundled entry in
  // place, tagged "(modified)" (script_folders.hpp).
  const std::function<void(QMenu*, const std::vector<ScriptFolderEntry>&, bool)> add_entries =
      [this, &add_entries](QMenu* menu, const std::vector<ScriptFolderEntry>& entries, bool mark) {
        for (const auto& entry : entries) {
          if (entry.is_folder) {
            auto* submenu = menu->addMenu(script_folder_display_name(entry.name));
            if (mark) {
              submenu->menuAction()->setProperty(kDynamicScriptActionProperty, true);
            }
            add_entries(submenu, entry.children, false);
            continue;
          }
          const auto text =
              entry.is_override ? tr("%1 (modified)").arg(entry.name) : entry.name;
          auto* action = menu->addAction(text);
          if (mark) {
            action->setProperty(kDynamicScriptActionProperty, true);
          }
          const auto path = entry.path;
          connect(action, &QAction::triggered, this, [this, path] { run_script_from_menu(path); });
        }
      };
  const auto scan = scan_scripts(bundled_scripts_directory(), user_scripts_directory());
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
      QCoreApplication::exit(ok ? 0 : 4);
    });
  });
}

}  // namespace patchy::ui
