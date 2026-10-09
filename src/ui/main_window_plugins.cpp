// MainWindow's legacy Photoshop plug-in support: folder scanning (a pure file
// read of each plug-in's property list, nothing is loaded), the category
// submenu tree under Plugins > Legacy Photoshop Plug-ins, and running a filter
// through the out-of-process host on the active layer, limited to the selection
// and undoable. See docs/plugins.md.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/qt_paths.hpp"

#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "core/layer_tree.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/background_workers.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/legacy_plugin_folder.hpp"
#include "ui/localization.hpp"
#include "ui/qt_geometry.hpp"
#ifdef Q_OS_WIN
#include "ui/legacy_plugin_run_dialog.hpp"
#include "ui/legacy_plugin_runner.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QRegion>
#include <QScopeGuard>
#include <QScreen>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// A PiPL name may end in Photoshop's menu ellipsis ("Kaleidoscope 2.1..."),
// which belongs in the menu item, not in a sentence or a window title.
QString legacy_plugin_sentence_name(const std::string& display_name) {
  auto name = QString::fromStdString(display_name).trimmed();
  while (name.endsWith(QLatin1Char('.')) || name.endsWith(QChar(0x2026))) {
    name.chop(1);
  }
  return name.trimmed();
}

std::string legacy_plugin_kind_name(LegacyPhotoshopPluginKind kind) {
  switch (kind) {
    case LegacyPhotoshopPluginKind::Filter8bf:
      return "filter";
    case LegacyPhotoshopPluginKind::Format8bi:
      return "file-format";
    case LegacyPhotoshopPluginKind::Automation8li:
      return "automation";
    case LegacyPhotoshopPluginKind::Unknown:
      return "unknown";
  }
  return "unknown";
}

constexpr char kLegacyPluginMenuEntryProperty[] = "patchy.legacyPluginMenuEntry";

// Every plug-in file under the roots (recursive), probed. Pure file reads, no
// Qt widgets: safe on a worker thread.
std::vector<std::pair<QString, LegacyPhotoshopPluginProbe>> probe_legacy_plugin_files(const QStringList& roots) {
  std::vector<std::pair<QString, LegacyPhotoshopPluginProbe>> probes;
  LegacyPhotoshopAdapter adapter;
  for (const auto& root : roots) {
    if (!QDir(root).exists()) {
      continue;
    }
    QStringList files;
    QDirIterator it(root, {QStringLiteral("*.8bf"), QStringLiteral("*.8bi"), QStringLiteral("*.8li")},
                    QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      files << it.next();
    }
    std::sort(files.begin(), files.end(), [](const QString& a, const QString& b) {
      return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for (const auto& file : files) {
      probes.emplace_back(file, adapter.probe(to_filesystem_path(file)));
    }
  }
  return probes;
}

}  // namespace

QStringList MainWindow::legacy_plugin_scan_roots() {
  QStringList roots;
  const auto app_dir = QCoreApplication::applicationDirPath();
  roots << QDir(app_dir).filePath(QStringLiteral("plugins"));
#ifndef Q_OS_WASM
  const auto data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!data_dir.isEmpty()) {
    roots << QDir(data_dir).filePath(QStringLiteral("plugins"));
  }
#endif
  roots << stored_legacy_plugin_folders();
  // Developer builds keep the committed test plug-ins next to the binary; the
  // packaged application does not ship them.
  const auto fixtures = QDir(app_dir).filePath(QStringLiteral("test-fixtures/photoshop-plugins"));
  if (QDir(fixtures).exists()) {
    roots << fixtures;
  }
  roots.removeDuplicates();
  return roots;
}

void MainWindow::open_legacy_plugins_folder() {
  QString error;
  if (!ensure_legacy_plugins_folder(&error)) {
    show_status_error(error);
    return;
  }
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(legacy_plugins_folder_path()))) {
    show_status_error(tr("Could not open the plug-ins folder."));
  }
}

void MainWindow::rescan_legacy_plugin_folders(QStringList* report) {
  // A synchronous scan supersedes any worker still running: its result is
  // dropped when it arrives (generation mismatch).
  const int generation = ++legacy_plugin_scan_generation_;
  legacy_plugin_scan_in_flight_ = false;
  legacy_plugin_scan_pending_ = false;
  auto probes = probe_legacy_plugin_files(legacy_plugin_scan_roots());
  legacy_plugins_.clear();
  plugin_host_.clear();
  for (auto& [path, probe] : probes) {
    add_legacy_plugin_entry(path, std::move(probe), report);
  }
  Q_UNUSED(generation);
  rebuild_legacy_plugins_menu();
}

bool MainWindow::legacy_plugin_scan_in_flight() const noexcept { return legacy_plugin_scan_in_flight_; }

void MainWindow::start_legacy_plugin_scan(bool announce) {
  legacy_plugin_scan_announce_ = legacy_plugin_scan_announce_ || announce;
  if (legacy_plugin_scan_in_flight_) {
    legacy_plugin_scan_pending_ = true;
    return;
  }
  legacy_plugin_scan_in_flight_ = true;
  legacy_plugin_scan_pending_ = false;
  const int generation = ++legacy_plugin_scan_generation_;
  rebuild_legacy_plugins_menu();  // the "Scanning..." note while the list is empty
  auto* app = QApplication::instance();
  QPointer<MainWindow> window(this);
  const auto roots = legacy_plugin_scan_roots();
  run_tracked_background_worker([app, window, roots, generation] {
    auto probes = std::make_shared<std::vector<std::pair<QString, LegacyPhotoshopPluginProbe>>>(
        probe_legacy_plugin_files(roots));
    if (app == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(
        app,
        [window, probes, generation] {
          if (window != nullptr) {
            window->finish_legacy_plugin_scan(generation, std::move(*probes));
          }
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::finish_legacy_plugin_scan(int generation,
                                           std::vector<std::pair<QString, LegacyPhotoshopPluginProbe>> probes) {
  if (generation != legacy_plugin_scan_generation_) {
    return;  // a later scan (sync or async) owns the list now
  }
  legacy_plugin_scan_in_flight_ = false;
  legacy_plugins_.clear();
  plugin_host_.clear();
  int available = 0;
  int skipped = 0;
  for (auto& [path, probe] : probes) {
    if (add_legacy_plugin_entry(path, std::move(probe), nullptr)) {
      ++available;
    } else {
      ++skipped;
    }
  }
  if (QApplication::activePopupWidget() == nullptr) {
    rebuild_legacy_plugins_menu();
  } else {
    // Rebuilding under an open menu would delete live entries; try again shortly.
    QTimer::singleShot(1000, this, [this] {
      if (QApplication::activePopupWidget() == nullptr) {
        rebuild_legacy_plugins_menu();
      }
    });
  }
  if (legacy_plugin_scan_announce_) {
    legacy_plugin_scan_announce_ = false;
    statusBar()->showMessage(skipped == 0 ? tr("Plug-in scan finished: %1 available").arg(available)
                                          : tr("Plug-in scan finished: %1 available, %2 not usable")
                                                .arg(available)
                                                .arg(skipped));
  }
  if (legacy_plugin_scan_pending_) {
    legacy_plugin_scan_pending_ = false;
    start_legacy_plugin_scan(false);
  }
}

bool MainWindow::register_legacy_plugin_path(const QString& path, QStringList* report, bool rebuild_menu) {
  LegacyPhotoshopAdapter adapter;
  const bool supported = add_legacy_plugin_entry(path, adapter.probe(to_filesystem_path(path)), report);
  if (rebuild_menu) {
    rebuild_legacy_plugins_menu();
  }
  return supported;
}

bool MainWindow::add_legacy_plugin_entry(const QString& path, LegacyPhotoshopPluginProbe probe, QStringList* report) {
  const auto file_name = QFileInfo(path).fileName();
  if (report != nullptr) {
    *report << tr("%1: %2 (%3, %4)")
                   .arg(file_name, translate_data_text(probe.reason),
                        QString::fromStdString(legacy_plugin_kind_name(probe.kind)),
                        QString::fromStdString(probe.architecture));
  }
  const auto absolute = QFileInfo(path).absoluteFilePath();
  const auto size = QFileInfo(path).size();
  for (const auto& existing : legacy_plugins_) {
    if (existing.path == absolute) {
      return existing.probe.supported;
    }
    // The same plug-in installed in two scanned folders (a copy of a pack in
    // the app folder and in the user folder) is listed once: same file name,
    // size, name and bitness is the same file.
    if (QFileInfo(existing.path).fileName().compare(file_name, Qt::CaseInsensitive) == 0 &&
        QFileInfo(existing.path).size() == size && existing.probe.display_name == probe.display_name &&
        existing.probe.architecture == probe.architecture) {
      if (report != nullptr) {
        *report << tr("%1: same plug-in as %2, listed once").arg(absolute, QDir::toNativeSeparators(existing.path));
      }
      return existing.probe.supported;
    }
  }

  // Identifiers come from the file stem (the persisted form since the first
  // release); a second file with the same stem gets a numbered suffix.
  const auto stem = QFileInfo(path).completeBaseName().toStdString();
  std::string identifier = "legacy.photoshop." + stem;
  for (int n = 2; std::any_of(legacy_plugins_.begin(), legacy_plugins_.end(),
                              [&](const LegacyPluginEntry& e) { return e.identifier == identifier; });
       ++n) {
    identifier = "legacy.photoshop." + stem + "." + std::to_string(n);
  }
  LegacyPluginEntry entry;
  entry.identifier = identifier;
  entry.path = absolute;
  entry.probe = std::move(probe);
  const bool supported = entry.probe.supported;
  if (supported) {
    PluginDescriptor descriptor;
    descriptor.kind = PATCHY_PLUGIN_FILTER;
    descriptor.identifier = identifier;
    descriptor.display_name = entry.probe.display_name;
    descriptor.path = to_filesystem_path(absolute);
    try {
      plugin_host_.register_plugin(std::move(descriptor));
    } catch (const std::exception&) {
    }
  }
  legacy_plugins_.push_back(std::move(entry));
  return supported;
}

const MainWindow::LegacyPluginEntry* MainWindow::find_legacy_plugin(std::string_view identifier) const noexcept {
  for (const auto& entry : legacy_plugins_) {
    if (entry.identifier == identifier) {
      return &entry;
    }
  }
  return nullptr;
}

void MainWindow::rebuild_legacy_plugins_menu() {
  if (legacy_plugins_menu_ == nullptr) {
    return;
  }
  const auto old_actions = legacy_plugins_menu_->actions();
  for (auto* action : old_actions) {
    if (!action->property(kLegacyPluginMenuEntryProperty).toBool()) {
      continue;
    }
    legacy_plugins_menu_->removeAction(action);
    if (auto* submenu = action->menu()) {
      for (auto* child : submenu->actions()) {
        unregister_document_action(child);
      }
      submenu->deleteLater();  // owns its menuAction
    } else {
      unregister_document_action(action);
      action->deleteLater();
    }
  }

  std::vector<const LegacyPluginEntry*> supported;
  for (const auto& entry : legacy_plugins_) {
    if (entry.probe.supported) {
      supported.push_back(&entry);
    }
  }
  if (supported.empty()) {
    auto* note = legacy_plugin_scan_in_flight_
                     ? legacy_plugins_menu_->addAction(tr("Scanning plug-in folders..."))
                     : legacy_plugins_menu_->addAction(tr("No plug-ins found (put .8bf files in the plugins folder)"));
    note->setObjectName(legacy_plugin_scan_in_flight_ ? QStringLiteral("legacyPluginsScanningNote")
                                                      : QStringLiteral("legacyPluginsEmptyNote"));
    note->setProperty(kLegacyPluginMenuEntryProperty, true);
    note->setEnabled(false);
    update_legacy_plugin_repeat_actions();  // the last plug-in may be gone with its folder
    return;
  }
  std::sort(supported.begin(), supported.end(), [](const LegacyPluginEntry* a, const LegacyPluginEntry* b) {
    const auto category = QString::fromStdString(a->probe.category)
                              .compare(QString::fromStdString(b->probe.category), Qt::CaseInsensitive);
    if (category != 0) {
      return category < 0;
    }
    return QString::fromStdString(a->probe.display_name)
               .compare(QString::fromStdString(b->probe.display_name), Qt::CaseInsensitive) < 0;
  });
  // A plug-in installed as both a 32-bit and a 64-bit file side by side (the
  // same name in the same category, in the same folder, as packs ship them)
  // is one menu entry, the 64-bit one: Photoshop only ever shows a user
  // plug-ins of its own bitness, and picking a bitness is not a choice anyone
  // wants in a menu. The 32-bit twin stays installed and listed for scripts.
  // A 32-bit-only plug-in, or a 32-bit copy kept in another folder on purpose,
  // shows as before.
  const auto twin_key = [](const LegacyPluginEntry& entry) {
    return QFileInfo(entry.path).absolutePath().toLower() + QLatin1Char('\n') +
           QString::fromStdString(entry.probe.category).trimmed().toLower() + QLatin1Char('\n') +
           QString::fromStdString(entry.probe.display_name).trimmed().toLower();
  };
  std::set<QString> native_twins;
  for (const auto* entry : supported) {
    if (entry->probe.architecture == "x64") {
      native_twins.insert(twin_key(*entry));
    }
  }
  std::map<QString, QMenu*> category_menus;
  for (const auto* entry : supported) {
    if (entry->probe.architecture != "x64" && native_twins.count(twin_key(*entry)) > 0) {
      continue;
    }
    QMenu* target = legacy_plugins_menu_;
    const auto category = QString::fromStdString(entry->probe.category).trimmed();
    if (!category.isEmpty()) {
      auto found = category_menus.find(category);
      if (found == category_menus.end()) {
        auto* submenu = legacy_plugins_menu_->addMenu(escape_qaction_ampersands(category));
        submenu->setObjectName(QStringLiteral("legacyPluginCategoryMenu"));
        submenu->menuAction()->setMenuRole(QAction::NoRole);  // submenus never merge on macOS (docs/platform.md)
        submenu->menuAction()->setProperty(kLegacyPluginMenuEntryProperty, true);
        found = category_menus.emplace(category, submenu).first;
      }
      target = found->second;
    }
    auto* action = target->addAction(escape_qaction_ampersands(QString::fromStdString(entry->probe.display_name)));
    const auto identifier = QString::fromStdString(entry->identifier);
    action->setData(identifier);
    action->setObjectName(QStringLiteral("legacyPluginAction"));
    action->setProperty(kLegacyPluginMenuEntryProperty, true);
    action->setProperty("patchy.channelViewBlocked", true);
    action->setStatusTip(tr("Run the %1 plug-in on the active layer").arg(legacy_plugin_sentence_name(entry->probe.display_name)));
    action->setIcon(simple_icon(QStringLiteral("8BF"), QColor(105, 185, 255)));
    action->setIconVisibleInMenu(false);
    connect(action, &QAction::triggered, this, [this, identifier] { run_legacy_plugin(identifier); });
    // The same tooltip a language switch would give it (retranslate_ui refreshes
    // every action), so fresh and switched windows agree on "Name..." entries.
    refresh_action_tooltip(action);
    register_document_action(action);
  }
  update_document_action_state();
}

void MainWindow::run_last_legacy_plugin(bool show_dialog) {
  if (last_legacy_plugin_identifier_.empty()) {
    return;
  }
  const auto* entry = find_legacy_plugin(last_legacy_plugin_identifier_);
  if (entry == nullptr || !entry->probe.supported) {
    // Gone since the last run (a rescan dropped its folder).
    update_legacy_plugin_repeat_actions();
    show_status_error(tr("The last plug-in is no longer available"));
    return;
  }
  run_legacy_plugin(QString::fromStdString(last_legacy_plugin_identifier_), show_dialog);
}

void MainWindow::update_legacy_plugin_repeat_actions() {
  if (plugins_repeat_last_action_ == nullptr || plugins_last_settings_action_ == nullptr) {
    return;
  }
  const auto* entry =
      last_legacy_plugin_identifier_.empty() ? nullptr : find_legacy_plugin(last_legacy_plugin_identifier_);
  // The identifier is kept while the plug-in is missing (a scan in flight, a
  // folder removed and added back); the commands just fall back to their
  // generic text until it is found again.
  const bool available = entry != nullptr && entry->probe.supported;
  const auto name = available ? escape_qaction_ampersands(legacy_plugin_sentence_name(entry->probe.display_name)) : QString();
  //: Plugins menu; %1 is the plug-in that ran last. Runs it again with its last settings, no dialog.
  plugins_repeat_last_action_->setText(available ? tr("Repeat %1").arg(name) : tr("Repeat Last Plug-in"));
  //: Plugins menu; %1 is the plug-in that ran last. Opens its settings dialog again, starting from the last settings.
  plugins_last_settings_action_->setText(available ? tr("%1 Settings...").arg(name) : tr("Last Plug-in Settings..."));
  // Not document actions: update_document_action_state would re-enable them
  // whenever a document is open; they also need a plug-in that ran.
  const bool enabled = available && has_active_document() && !preview_dialog_edit_locked();
  plugins_repeat_last_action_->setEnabled(enabled);
  plugins_last_settings_action_->setEnabled(enabled);
  refresh_action_tooltip(plugins_repeat_last_action_);
  refresh_action_tooltip(plugins_last_settings_action_);
}

void MainWindow::run_legacy_plugin(QString identifier, bool show_dialog) {
  if (canvas_ != nullptr && canvas_->quick_mask_active()) {
    show_status_error(tr("Filters are unavailable in Quick Mask mode"));
    return;
  }
  if (canvas_ != nullptr &&
      (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::DocumentChannel ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentRed ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentGreen ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentBlue)) {
    show_status_error(tr("Filters are unavailable while viewing a document channel"));
    return;
  }
  const auto* entry = find_legacy_plugin(identifier.toStdString());
  if (entry == nullptr || !entry->probe.supported) {
    return;
  }
  auto* session = active_session();
  if (session == nullptr) {
    return;
  }
  select_only_layer_if_none_active();
  const auto active = document().active_layer_id();
  if (!active.has_value()) {
    show_status_error(tr("Select a pixel layer before running the plug-in"));
    return;
  }
  // A plug-in sees one layer's pixels (that is the whole of the filter
  // interface); with several selected, say so instead of quietly using the
  // active one.
  if (selected_or_active_layer_ids().size() > 1) {
    (void)show_warning_message(this, tr("Legacy Photoshop Plug-in"),
                               tr("Plug-ins work on one layer at a time. Select a single layer and run it again."),
                               QMessageBox::Ok, QMessageBox::Ok, QStringLiteral("legacyPluginOneLayerMessageBox"));
    return;
  }
  const auto* layer = std::as_const(document()).find_layer(*active);
  if (layer == nullptr) {
    show_status_error(tr("Select a pixel layer before running the plug-in"));
    return;
  }
  const auto display_name = legacy_plugin_sentence_name(entry->probe.display_name);
  // A text or shape layer is offered for rasterizing first, as Photoshop does
  // for its filters (a plug-in only ever sees pixels); Cancel leaves it alone.
  if (layer->kind() == LayerKind::Text || layer_pixels_are_procedural(*layer)) {
    if (!prompt_rasterize_procedural_layer(*active, display_name, /*offer_convert_to_smart_object=*/false)) {
      return;
    }
    layer = std::as_const(document()).find_layer(*active);
    if (layer == nullptr) {
      return;
    }
  }
  if (layer->kind() != LayerKind::Pixel) {
    show_status_error(tr("Select an editable 8-bit pixel layer before running the plug-in"));
    return;
  }
  const auto& source_pixels = layer->pixels();
  if (source_pixels.format().bit_depth == BitDepth::Float32 || source_pixels.format().channels < 3) {
    show_status_error(tr("Select an editable 8-bit pixel layer before running the plug-in"));
    return;
  }
  if (layer_pixels_are_procedural(*layer)) {
    show_status_error(tr("Rasterize Text, Smart Object, and Shape layers before editing their pixels"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }

  QString error;
  // The plug-in's window (another process) takes the foreground; when it
  // closes, Windows activates this window without a focus widget, so hotkeys
  // such as Ctrl+Z went nowhere until the canvas was clicked. Put focus back
  // where it was.
  QPointer<QWidget> previous_focus = QApplication::focusWidget();
  // Repeat (show_dialog false) reuses the last settings without the dialog, as
  // Photoshop's "repeat last filter" does; a dialog the plug-in opens anyway
  // (nothing stored yet) is the user's to answer, never auto-accepted.
  const auto status = apply_legacy_plugin(
      *session, *active, *entry, show_dialog, [this, display_name] { push_undo_snapshot(tr("Plug-in: %1").arg(display_name)); },
      &error, QString(), /*auto_accept_dialogs=*/false);
  activateWindow();
  if (QWidget* focus = previous_focus != nullptr && previous_focus->isEnabled() && previous_focus->isVisible()
                           ? previous_focus.data()
                           : static_cast<QWidget*>(fallback_active_canvas());
      focus != nullptr) {
    focus->setFocus(Qt::OtherFocusReason);
  }
  switch (status) {
    case LegacyPluginApplyStatus::Applied: {
      // A plug-in never previews on the canvas, so the result is the first
      // look: say how to take it back.
      const auto undo_key = undo_action_ != nullptr ? undo_action_->shortcut() : QKeySequence();
      statusBar()->showMessage(undo_key.isEmpty()
                                   ? tr("Applied %1").arg(display_name)
                                   //: %1 is the plug-in's name, %2 the Undo shortcut ("Ctrl+Z").
                                   : tr("Applied %1 (%2 undoes it)").arg(display_name, undo_key.toString(QKeySequence::NativeText)));
      break;
    }
    case LegacyPluginApplyStatus::NoChange:
      statusBar()->showMessage(tr("%1 made no changes").arg(display_name));
      break;
    case LegacyPluginApplyStatus::Cancelled:
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      break;
    case LegacyPluginApplyStatus::Error:
      (void)show_warning_message(this, tr("Legacy Photoshop Plug-in"),
                                 tr("%1 could not run.\n\n%2").arg(display_name, error), QMessageBox::Ok,
                                 QMessageBox::Ok, QStringLiteral("legacyPluginErrorMessageBox"));
      break;
  }
}

MainWindow::LegacyPluginApplyStatus MainWindow::apply_legacy_plugin(DocumentSession& session, LayerId layer_id,
                                                                    const LegacyPluginEntry& entry, bool show_dialog,
                                                                    const std::function<void()>& before_write,
                                                                    QString* error, const QString& capture_dialog_path,
                                                                    bool auto_accept_dialogs) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return LegacyPluginApplyStatus::Error;
  };
#ifndef Q_OS_WIN
  Q_UNUSED(session);
  Q_UNUSED(layer_id);
  Q_UNUSED(entry);
  Q_UNUSED(show_dialog);
  Q_UNUSED(before_write);
  Q_UNUSED(capture_dialog_path);
  Q_UNUSED(auto_accept_dialogs);
  return fail(tr("Legacy Photoshop plug-ins run on Windows only."));
#else
  auto* layer = session.document.find_layer(layer_id);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return fail(tr("Select an editable 8-bit pixel layer before running the plug-in"));
  }
  const auto& layer_pixels = std::as_const(*layer).pixels();
  // A 16-bit layer (docs/high-bit-depth.md) runs the plug-in on its 8-bit copy; the
  // change folds back at depth below. 32-bit layers stay unsupported.
  const auto deep_layer = layer_pixels.format().bit_depth == BitDepth::UInt16;
  const auto narrowed =
      deep_layer ? convert_pixel_buffer_depth(layer_pixels, BitDepth::UInt8, SampleKind::Color) : PixelBuffer{};
  const auto& source = deep_layer ? narrowed : layer_pixels;
  if (source.empty() || source.format().bit_depth != BitDepth::UInt8 || source.format().channels < 3) {
    return fail(tr("Select an editable 8-bit pixel layer before running the plug-in"));
  }
  const auto channels = static_cast<int>(source.format().channels);
  const auto width = source.width();
  const auto height = source.height();
  const auto bounds = layer->bounds();
  const auto& pipl = entry.probe.pipl;

  // The selection, in layer coordinates, bounds the filter rectangle; its
  // coverage limits the result afterwards (hard or feathered alike).
  QRegion selection;
  if (session.canvas != nullptr) {
    selection = session.canvas->selected_document_region();
  }
  const QRect layer_rect(bounds.x, bounds.y, width, height);
  QRect filter_rect = layer_rect;
  std::vector<std::uint8_t> coverage;
  bool has_selection = false;
  if (!selection.isEmpty()) {
    const auto selected = selection.boundingRect().intersected(layer_rect);
    if (selected.isEmpty()) {
      return fail(tr("The selection does not touch the active layer."));
    }
    has_selection = true;
    filter_rect = selected;
    const auto mask = selection_mask_pixels(*session.canvas, layer_rect);
    coverage.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    for (std::int32_t y = 0; y < height; ++y) {
      const auto row = mask.row(y);
      std::memcpy(coverage.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width), row.data(),
                  static_cast<std::size_t>(width));
    }
  }

  // Filter case: the plug-in's property list says whether it takes a
  // transparency plane; a layer without alpha, or a plug-in that cannot, gets
  // the flat cases and keeps its alpha untouched.
  const bool protect_alpha = layer_id_locks_transparent_pixels(layer_id);
  int filter_case = has_selection ? pipl::kFilterCaseFlatImageWithSelection : pipl::kFilterCaseFlatImageNoSelection;
  int planes = 3;
  if (channels >= 4) {
    const int editable = has_selection ? pipl::kFilterCaseEditableTransparencyWithSelection
                                       : pipl::kFilterCaseEditableTransparencyNoSelection;
    const int protected_case = has_selection ? pipl::kFilterCaseProtectedTransparencyWithSelection
                                             : pipl::kFilterCaseProtectedTransparencyNoSelection;
    if (protect_alpha && pipl.supports_filter_case(protected_case)) {
      filter_case = protected_case;
      planes = 4;
    } else if (pipl.supports_filter_case(editable)) {
      filter_case = editable;
      planes = 4;
    }
  }

  const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<std::uint8_t> input(pixel_count * static_cast<std::size_t>(planes));
  for (std::int32_t y = 0; y < height; ++y) {
    const auto row = source.row(y);
    auto* dst = input.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * static_cast<std::size_t>(planes);
    for (std::int32_t x = 0; x < width; ++x) {
      const auto* px = row.data() + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
      for (int p = 0; p < planes; ++p) {
        dst[static_cast<std::size_t>(x) * static_cast<std::size_t>(planes) + static_cast<std::size_t>(p)] =
            p < channels ? px[p] : 255;
      }
    }
  }
  std::vector<std::uint8_t> output = input;

  LegacyPluginRunInput run;
  run.helper_executable = legacy_plugin_helper_path(entry.probe.architecture);
  run.plugin_path = entry.path;
  run.entry_point = entry.probe.entry_point;
  run.show_dialog = show_dialog;
  // The caller decides (main_window.hpp): scripts that asked for no dialog get
  // any dialog a plug-in opens anyway answered so they never block; the menu's
  // Repeat leaves such a dialog to the user.
  run.auto_accept_dialogs = auto_accept_dialogs;
  run.capture_dialog_path = capture_dialog_path;
  run.protect_alpha = protect_alpha && planes == 4;
  run.parent_window = QGuiApplication::platformName() == QLatin1String("offscreen")
                          ? 0
                          : static_cast<std::uintptr_t>(winId());
  {
    const auto screen_size = stored_legacy_plugin_screen_size();
    run.screen_max_width = screen_size.first;
    run.screen_max_height = screen_size.second;
  }
  //: Title of the movable window a full-screen plug-in interface is shown in; %1 is the plug-in's name.
  run.window_title = tr("%1 via Patchy").arg(legacy_plugin_sentence_name(entry.probe.display_name));
  run.width = width;
  run.height = height;
  run.planes = planes;
  run.filter_rect = filter_rect.translated(-bounds.x, -bounds.y);
  run.filter_case = filter_case;
  run.case_info = pipl.case_info_for(filter_case);
  run.input = &input;
  run.output = &output;
  run.mask = has_selection ? &coverage : nullptr;
  if (session.canvas != nullptr) {
    const auto fg = session.canvas->primary_color();
    const auto bg = session.canvas->secondary_color();
    run.foreground = {static_cast<std::uint8_t>(fg.red()), static_cast<std::uint8_t>(fg.green()),
                      static_cast<std::uint8_t>(fg.blue())};
    run.background = {static_cast<std::uint8_t>(bg.red()), static_cast<std::uint8_t>(bg.green()),
                      static_cast<std::uint8_t>(bg.blue())};
  }
  run.resolution_fixed = static_cast<std::int32_t>(std::clamp(text_size_ppi(session.document), 1.0, 30000.0) * 65536.0);
  run.document_title = session.title;
  if (const auto stored = legacy_plugin_parameters_.find(entry.identifier); stored != legacy_plugin_parameters_.end()) {
    run.parameters = stored->second;
  }

  const auto display_name = legacy_plugin_sentence_name(entry.probe.display_name);
  // No canvas "Processing..." overlay here: the companion box is the one
  // indicator, next to the plug-in's own window. It follows the helper's
  // phase reports and the plug-in's window (legacy_plugin_run_dialog.hpp): calm
  // "adjust its settings and click OK" text while the plug-in waits for the
  // user, a busy bar or percentage while it applies. Shown once the run
  // outlasts a quick filter, or as soon as the plug-in shows a window.
  LegacyPluginRunDialog progress(display_name, this);
  QElapsedTimer elapsed;
  elapsed.start();
  LegacyPluginRunCallbacks callbacks;
  callbacks.plugin_window = [&progress](const QRect& rect) { progress.set_plugin_window(rect); };
  callbacks.phase = [&progress](LegacyPluginPhase phase) { progress.set_phase(phase); };
  callbacks.progress = [&progress](int done, int total) { progress.set_progress(done, total); };
  callbacks.cancelled = [&progress] { return progress.stop_requested(); };
  callbacks.raise_plugin_window = [&progress] { return progress.take_show_window_request(); };
  callbacks.tick = [&] {
    // A plug-in that never reports progress would keep the box (and its stop
    // button) hidden; show it once the run outlasts a quick filter.
    if (elapsed.elapsed() > kFilterProgressMinimumDurationMs) {
      progress.reveal();
    }
  };
  const auto result = run_legacy_plugin_out_of_process(run, callbacks);
  progress.hide();
  // The plug-in's windows lived in another process but were owned by this
  // window, which shares input state with them for the duration: a modal
  // dialog of the plug-in disables its owner (this window) and a helper that
  // ends mid-dialog never re-enables it, and the keyboard focus the plug-in
  // held is not handed back. Put this window back in charge of its own input
  // unconditionally, at the Win32 level, before Qt's focus is restored.
  {
    const HWND self = reinterpret_cast<HWND>(winId());
    if (!IsWindowEnabled(self)) {
      EnableWindow(self, TRUE);
    }
    if (GetForegroundWindow() != self) {
      SetForegroundWindow(self);
    }
    SetActiveWindow(self);
    // A focus cycle, not just SetFocus: Qt marks its window active from
    // WM_SETFOCUS, and SetFocus on a window that already holds the focus
    // sends nothing. When the plug-in closed its window, the focus came back
    // here without a message (attached input queues), so Qt still believed
    // no window was active and every shortcut was dropped until the user
    // switched to another app and back.
    SetFocus(nullptr);
    SetFocus(self);
  }
  if (!result.parameters.isEmpty()) {
    legacy_plugin_parameters_[entry.identifier] = result.parameters;
  }
  if (result.status == LegacyPluginRunStatus::Ok) {
    // A completed run (changed pixels or not) is what Repeat and "Settings..."
    // run again; a cancelled or failed one is not.
    last_legacy_plugin_identifier_ = entry.identifier;
    update_legacy_plugin_repeat_actions();
  }
  if (result.status == LegacyPluginRunStatus::Cancelled) {
    return LegacyPluginApplyStatus::Cancelled;
  }
  if (result.status != LegacyPluginRunStatus::Ok) {
    return fail(result.message);
  }

  // Blend the result back through the selection coverage, then compare.
  const auto local = run.filter_rect;
  bool changed = false;
  for (int y = local.top(); y < local.top() + local.height(); ++y) {
    for (int x = local.left(); x < local.left() + local.width(); ++x) {
      const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x));
      const int cov = has_selection ? coverage[index] : 255;
      auto* out = output.data() + index * static_cast<std::size_t>(planes);
      const auto* in = input.data() + index * static_cast<std::size_t>(planes);
      if (cov == 0) {
        // Inside the selection's bounding box but outside its shape: the
        // plug-in wrote here (it works on the whole rectangle), and the
        // write-back below copies the whole rectangle, so the original
        // pixel has to be put back explicitly. Skipping it left the plug-in's
        // output there and turned a lasso into its bounding box.
        std::memcpy(out, in, static_cast<std::size_t>(planes));
        continue;
      }
      for (int p = 0; p < planes; ++p) {
        const int blended = cov == 255 ? out[p] : (in[p] * (255 - cov) + out[p] * cov + 127) / 255;
        if (blended != in[p]) {
          changed = true;
        }
        out[p] = static_cast<std::uint8_t>(blended);
      }
    }
  }
  if (!changed) {
    return LegacyPluginApplyStatus::NoChange;
  }

  if (before_write) {
    before_write();
  }
  layer = session.document.find_layer(layer_id);
  if (layer == nullptr) {
    return fail(tr("The layer no longer exists."));
  }
  const auto write_output = [&](PixelBuffer& pixels) {
    (void)pixels.data();  // detach shared storage once before the writes
    for (int y = local.top(); y < local.top() + local.height(); ++y) {
      auto row = pixels.row(y);
      for (int x = local.left(); x < local.left() + local.width(); ++x) {
        const auto index =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x));
        const auto* out = output.data() + index * static_cast<std::size_t>(planes);
        auto* px = row.data() + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
        px[0] = out[0];
        px[1] = out[1];
        px[2] = out[2];
        if (planes == 4 && channels >= 4 && !run.protect_alpha) {
          px[3] = out[3];
        }
      }
    }
  };
  if (std::as_const(*layer).pixels().format().bit_depth == BitDepth::UInt8) {
    write_output(layer->pixels());
  } else {
    apply_eight_bit_edit_at_depth(layer->pixels(), write_output);
  }
  if (session.canvas != nullptr) {
    session.canvas->document_changed(filter_rect);
  }
  return LegacyPluginApplyStatus::Applied;
#endif
}

}  // namespace patchy::ui
