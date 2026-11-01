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

QString sentence_name(const QString& label) {
  auto name = label.trimmed();
  while (name.endsWith(QLatin1Char('.')) || name.endsWith(QChar(0x2026))) {
    name.chop(1);
  }
  return name.trimmed();
}

QString plugin_language() {
  const auto code = LocalizationManager::instance().current_language();
  return code.isEmpty() ? QStringLiteral("en") : code;
}

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
      connect(action, &QAction::triggered, this, [this, identifier] { run_gimp_plugin(identifier); });
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

void MainWindow::run_gimp_plugin(const QString& identifier, int run_mode) {
  if (canvas_ != nullptr && canvas_->quick_mask_active()) {
    show_status_error(tr("Filters are unavailable in Quick Mask mode"));
    return;
  }
  if (canvas_ != nullptr &&
      (canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::DocumentChannel ||
       CanvasWidget::is_component_preview(canvas_->layer_edit_target()))) {
    show_status_error(tr("Filters are unavailable while viewing a document channel"));
    return;
  }
  const auto* entry = find_gimp_plugin(identifier);
  if (entry == nullptr || entry->use != QLatin1String("layer")) {
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
  if (selected_or_active_layer_ids().size() > 1) {
    (void)show_warning_message(this, tr("GIMP 3 Plug-in"),
                               tr("Plug-ins work on one layer at a time. Select a single layer and run it again."),
                               QMessageBox::Ok, QMessageBox::Ok, QStringLiteral("gimpPluginOneLayerMessageBox"));
    return;
  }
  const auto* layer = std::as_const(document()).find_layer(*active);
  if (layer == nullptr) {
    show_status_error(tr("Select a pixel layer before running the plug-in"));
    return;
  }
  const auto display_name = sentence_name(entry->label);
  if (layer->kind() == LayerKind::Text || layer_pixels_are_procedural(*layer)) {
    if (!prompt_rasterize_procedural_layer(*active, display_name, /*offer_convert_to_smart_object=*/false)) {
      return;
    }
    layer = std::as_const(document()).find_layer(*active);
    if (layer == nullptr) {
      return;
    }
  }
  if (layer->kind() != LayerKind::Pixel || layer_pixels_are_procedural(*layer)) {
    show_status_error(tr("Select an editable pixel layer before running the plug-in"));
    return;
  }
  if (layer_id_locks_image_pixels(*active)) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  QPointer<QWidget> previous_focus = QApplication::focusWidget();
  QString error;
  GimpPluginApplyOptions options;
  options.run_mode = run_mode;
  const auto status = apply_gimp_plugin(
      *session, *active, *entry, options,
      [this, display_name] { push_undo_snapshot(tr("GIMP Plug-in: %1").arg(display_name)); }, &error);
#ifdef Q_OS_WIN
  {
    // The plug-in's dialog (another process) held the foreground and may have
    // disabled this window as its owner; put this window back in charge of its input.
    const HWND self = reinterpret_cast<HWND>(winId());
    if (!IsWindowEnabled(self)) {
      EnableWindow(self, TRUE);
    }
    if (GetForegroundWindow() != self) {
      SetForegroundWindow(self);
    }
    SetActiveWindow(self);
    SetFocus(nullptr);
    SetFocus(self);
  }
#endif
  activateWindow();
  if (QWidget* focus = previous_focus != nullptr && previous_focus->isEnabled() && previous_focus->isVisible()
                           ? previous_focus.data()
                           : static_cast<QWidget*>(fallback_active_canvas());
      focus != nullptr) {
    focus->setFocus(Qt::OtherFocusReason);
  }
  switch (status) {
    case GimpPluginApplyStatus::Applied: {
      const auto undo_key = undo_action_ != nullptr ? undo_action_->shortcut() : QKeySequence();
      statusBar()->showMessage(undo_key.isEmpty()
                                   ? tr("Applied %1").arg(display_name)
                                   : tr("Applied %1 (%2 undoes it)").arg(display_name, undo_key.toString(QKeySequence::NativeText)));
      break;
    }
    case GimpPluginApplyStatus::NoChange:
      statusBar()->showMessage(tr("%1 made no changes").arg(display_name));
      break;
    case GimpPluginApplyStatus::Cancelled:
      statusBar()->showMessage(tr("Cancelled %1").arg(display_name));
      break;
    case GimpPluginApplyStatus::Error:
      (void)show_warning_message(this, tr("GIMP 3 Plug-in"), error, QMessageBox::Ok, QMessageBox::Ok,
                                 QStringLiteral("gimpPluginErrorMessageBox"));
      break;
  }
}

MainWindow::GimpPluginApplyStatus MainWindow::apply_gimp_plugin(DocumentSession& session, LayerId layer_id,
                                                                const GimpPluginProcedure& entry,
                                                                const GimpPluginApplyOptions& options,
                                                                const std::function<void()>& before_write,
                                                                QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return GimpPluginApplyStatus::Error;
  };
  const auto display_name = sentence_name(entry.label);
  if (gimp_plugins_ == nullptr || !gimp_plugins_->install.has_value()) {
    return fail(tr("GIMP 3 was not found, so its plug-ins cannot run."));
  }
  if (entry.use != QLatin1String("layer")) {
    return fail(tr("%1 cannot run in Patchy: %2").arg(display_name, gimp_plugin_reason_text(entry.reason)));
  }
  auto* layer = session.document.find_layer(layer_id);
  if (layer == nullptr || layer->kind() != LayerKind::Pixel) {
    return fail(tr("Select an editable pixel layer before running the plug-in"));
  }
  const auto& source = std::as_const(*layer).pixels();
  const auto format = source.format();
  if (source.empty()) {
    return GimpPluginApplyStatus::NoChange;
  }
  if (format.color_mode != ColorMode::RGB || (format.channels != 3 && format.channels != 4)) {
    return fail(tr("GIMP plug-ins run on RGB layers. Convert the document to RGB first."));
  }
  const auto width = source.width();
  const auto height = source.height();
  const auto bounds = layer->bounds();
  // A layer without alpha that may hold transparency is handed over with an opaque alpha
  // channel, as GIMP plug-ins expect to add one (Pagecurl); the result keeps alpha only
  // when it uses it. A layer whose transparency is locked (the Background) stays RGB.
  const bool expand_alpha = format.channels == 3 && !layer_id_locks_transparent_pixels(layer_id);
  const std::size_t channel_bytes = bytes_per_channel(format.bit_depth);
  const std::size_t layer_pixel_bytes = bytes_per_pixel(format);
  const std::size_t pixel_bytes = expand_alpha ? channel_bytes * 4 : layer_pixel_bytes;
  const std::size_t row_bytes = static_cast<std::size_t>(width) * pixel_bytes;
  std::vector<std::uint8_t> opaque(channel_bytes);
  if (format.bit_depth == BitDepth::UInt8) {
    opaque[0] = 0xFF;
  } else if (format.bit_depth == BitDepth::UInt16) {
    opaque[0] = opaque[1] = 0xFF;
  } else {
    const float one = 1.0f;
    std::memcpy(opaque.data(), &one, sizeof one);
  }
  std::vector<std::uint8_t> input(row_bytes * static_cast<std::size_t>(height));
  for (std::int32_t y = 0; y < height; ++y) {
    const auto row = source.row(y);
    auto* out = input.data() + static_cast<std::size_t>(y) * row_bytes;
    if (!expand_alpha) {
      std::memcpy(out, row.data(), row_bytes);
      continue;
    }
    for (std::int32_t x = 0; x < width; ++x) {
      std::memcpy(out + static_cast<std::size_t>(x) * pixel_bytes, row.data() + static_cast<std::size_t>(x) * layer_pixel_bytes,
                  layer_pixel_bytes);
      std::memcpy(out + static_cast<std::size_t>(x) * pixel_bytes + layer_pixel_bytes, opaque.data(), channel_bytes);
    }
  }

  // The selection, in layer pixels: its coverage limits where results land.
  const QRect layer_rect(bounds.x, bounds.y, width, height);
  std::vector<std::uint8_t> coverage;
  QRect mask_bounds;
  if (session.canvas != nullptr) {
    const auto selection = session.canvas->selected_document_region();
    if (!selection.isEmpty()) {
      const auto selected = selection.boundingRect().intersected(layer_rect);
      if (selected.isEmpty()) {
        return fail(tr("The selection does not touch the active layer."));
      }
      mask_bounds = selected.translated(-bounds.x, -bounds.y);
      const auto mask = selection_mask_pixels(*session.canvas, layer_rect);
      coverage.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
      for (std::int32_t y = 0; y < height; ++y) {
        std::memcpy(coverage.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width), mask.row(y).data(),
                    static_cast<std::size_t>(width));
      }
    }
  }

  QString launch_error;
  auto launch = gimp_plugin_launch_spec(*gimp_plugins_->install, entry.plugin_file, plugin_language(), &launch_error);
  if (!launch.has_value()) {
    return fail(tr("%1 could not be started: %2").arg(display_name, launch_error));
  }

  GimpPluginRunInput run;
  run.helper = gimp_plugin_helper_path();
  run.launch = std::move(*launch);
  run.procedure = entry.procedure;
  run.install = entry.install;
  run.run_mode = options.run_mode;
  run.arguments = options.arguments;
  run.argument_count = options.argument_count;
  run.image_width = session.document.width();
  run.image_height = session.document.height();
  run.layer_x = bounds.x;
  run.layer_y = bounds.y;
  run.width = width;
  run.height = height;
  run.bit_depth = static_cast<std::uint32_t>(format.bit_depth);
  run.channels = expand_alpha ? 4u : format.channels;
  run.protect_alpha = format.channels == 4 && layer_id_locks_transparent_pixels(layer_id);
  run.layer_name = QString::fromStdString(layer->name());
  run.image_name = session.title;
  run.resolution = text_size_ppi(session.document);
  run.input = &input;
  run.mask = coverage.empty() ? nullptr : &coverage;
  run.mask_bounds = mask_bounds;
  if (session.canvas != nullptr) {
    const auto fg = session.canvas->primary_color();
    const auto bg = session.canvas->secondary_color();
    const double f[4] = {fg.redF(), fg.greenF(), fg.blueF(), 1.0};
    const double b[4] = {bg.redF(), bg.greenF(), bg.blueF(), 1.0};
    std::memcpy(run.foreground, f, sizeof f);
    std::memcpy(run.background, b, sizeof b);
  }
  run.parent_window =
      QGuiApplication::platformName() == QLatin1String("offscreen") ? 0 : static_cast<std::uint64_t>(winId());
  run.language = plugin_language();
  run.timeout_ms = options.timeout_ms;
  for (const auto& [name, bytes] : gimp_plugin_stored_data_) {
    run.stored_data.emplace_back(name, bytes);
  }
  {
    // Patchy's RGB is sRGB; GIMP's colors carry their profile.
    static const auto srgb = create_rgb_matrix_profile(RgbMatrixProfileSpec{"sRGB (Patchy)"});
    run.color_profile = srgb;
  }
  run.capture_dialog_path = options.capture_dialog;
  run.answer_dialog = options.answer_dialog;
  run.trace = options.trace || qEnvironmentVariableIsSet("PATCHY_GIMP_TRACE");
  run.wire_log = options.wire_log.isEmpty() ? qEnvironmentVariable("PATCHY_GIMP_WIRE_LOG") : options.wire_log;

  // Live updates go straight into the layer and are taken back afterwards; the
  // original pixels are kept (shared storage, no copy until the first write).
  const PixelBuffer original = source;
  bool preview_shown = false;
  GimpPluginRunDialog box(display_name, this);
  QElapsedTimer elapsed;
  elapsed.start();
  GimpPluginRunCallbacks callbacks;
  callbacks.update = [&](const QRect& rect, const std::uint8_t* pixels) {
    auto* target = session.document.find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    const auto area = rect.intersected(QRect(0, 0, width, height));
    if (area.isEmpty() || std::as_const(*target).pixels().width() != width ||
        std::as_const(*target).pixels().height() != height) {
      return;
    }
    auto& dst = target->pixels();
    (void)dst.data();  // detach shared storage once before the row writes
    for (int y = area.top(); y <= area.bottom(); ++y) {
      auto* out = dst.row(y).data() + static_cast<std::size_t>(area.left()) * layer_pixel_bytes;
      const auto* in = pixels + static_cast<std::size_t>(y) * row_bytes + static_cast<std::size_t>(area.left()) * pixel_bytes;
      if (!expand_alpha) {
        std::memcpy(out, in, static_cast<std::size_t>(area.width()) * pixel_bytes);
        continue;
      }
      for (int x = 0; x < area.width(); ++x) {  // the preview shows the colors; alpha lands on OK
        std::memcpy(out + static_cast<std::size_t>(x) * layer_pixel_bytes, in + static_cast<std::size_t>(x) * pixel_bytes,
                    layer_pixel_bytes);
      }
    }
    preview_shown = true;
    if (session.canvas != nullptr) {
      session.canvas->document_changed(area.translated(bounds.x, bounds.y));
    }
  };
  callbacks.progress = [&](double fraction, const QString& text) { box.set_progress(fraction, text); };
  callbacks.plugin_window = [&](const QRect& rect) { box.set_plugin_window(rect); };
  callbacks.cancelled = [&] { return box.stop_requested(); };
  callbacks.tick = [&] {
    if (elapsed.elapsed() > kFilterProgressMinimumDurationMs) {
      box.reveal();
    }
  };
  begin_preview_dialog_edit_lock();
  const auto result = run_gimp_plugin_out_of_process(run, callbacks);
  box.hide();
  end_preview_dialog_edit_lock();
  for (const auto& [name, bytes] : result.stored_data) {
    gimp_plugin_stored_data_[name] = bytes;
  }
  layer = session.document.find_layer(layer_id);
  if (preview_shown && layer != nullptr) {
    layer->set_pixels(original);
    if (session.canvas != nullptr) {
      session.canvas->document_changed(layer_rect);
    }
  }
  if (result.status == GimpPluginRunStatus::Cancelled) {
    return GimpPluginApplyStatus::Cancelled;
  }
  if (result.status != GimpPluginRunStatus::Ok) {
    return fail(gimp_plugin_error_text(display_name, result));
  }
  // Colors the plug-in picked become Patchy's, as in GIMP (Border Average).
  if (session.canvas != nullptr && result.foreground_changed) {
    session.canvas->set_primary_color(QColor::fromRgbF(static_cast<float>(result.foreground[0]),
                                                       static_cast<float>(result.foreground[1]),
                                                       static_cast<float>(result.foreground[2])));
  }
  if (session.canvas != nullptr && result.background_changed) {
    session.canvas->set_secondary_color(QColor::fromRgbF(static_cast<float>(result.background[0]),
                                                         static_cast<float>(result.background[1]),
                                                         static_cast<float>(result.background[2])));
  }
  if (layer == nullptr) {
    return fail(tr("The layer no longer exists."));
  }
  if (result.output.size() != input.size() || !result.changed || result.output == input) {
    return GimpPluginApplyStatus::NoChange;
  }
  if (before_write) {
    before_write();
  }
  layer = session.document.find_layer(layer_id);
  if (layer == nullptr) {
    return fail(tr("The layer no longer exists."));
  }
  bool uses_alpha = false;
  if (expand_alpha) {
    for (std::size_t i = layer_pixel_bytes; i < result.output.size() && !uses_alpha; i += pixel_bytes) {
      uses_alpha = std::memcmp(result.output.data() + i, opaque.data(), channel_bytes) != 0;
    }
  }
  if (uses_alpha) {
    // The plug-in made pixels transparent: the layer gains an alpha channel.
    PixelBuffer with_alpha(width, height, PixelFormat{format.color_mode, format.bit_depth, 4});
    for (std::int32_t y = 0; y < height; ++y) {
      std::memcpy(with_alpha.row(y).data(), result.output.data() + static_cast<std::size_t>(y) * row_bytes, row_bytes);
    }
    layer->set_pixels(std::move(with_alpha));
  } else {
    auto& pixels = layer->pixels();
    (void)pixels.data();
    for (std::int32_t y = 0; y < height; ++y) {
      const auto* in = result.output.data() + static_cast<std::size_t>(y) * row_bytes;
      auto* out = pixels.row(y).data();
      if (!expand_alpha) {
        std::memcpy(out, in, row_bytes);
        continue;
      }
      for (std::int32_t x = 0; x < width; ++x) {
        std::memcpy(out + static_cast<std::size_t>(x) * layer_pixel_bytes, in + static_cast<std::size_t>(x) * pixel_bytes,
                    layer_pixel_bytes);
      }
    }
  }
  if (session.canvas != nullptr) {
    session.canvas->document_changed(layer_rect);
  }
  return GimpPluginApplyStatus::Applied;
}

}  // namespace patchy::ui
