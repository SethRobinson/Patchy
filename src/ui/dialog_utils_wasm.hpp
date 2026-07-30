#pragma once

#include <QPoint>
#include <QSize>
#include <QString>

#include <functional>

class QAction;
class QDialog;
class QMenu;
class QWidget;

// Browser-side file access for the WebAssembly build (docs/wasm.md, step 3).
// dialog_utils.cpp delegates its file pickers here under Q_OS_WASM; the
// implementation TU (dialog_utils_wasm.cpp) is only compiled for Emscripten.
//
// The shared shape: real files never leave the browser sandbox, so opens copy
// the picked bytes into MEMFS and return a path for the existing path-based
// pipeline, and saves let the existing writers hit a MEMFS path whose bytes
// are then handed to the browser as a download.
namespace patchy::ui::wasm_files {

// Runs the browser file picker, copies the picked file to
// /opened/<n>/<original name> in MEMFS, and returns that path (empty on
// cancel). Blocks in a nested event loop (Asyncify), so call sites keep their
// synchronous shape. `caption` titles the fallback waiting dialog shown on
// browsers whose picker cannot report cancellation.
[[nodiscard]] QString pick_open_file(QWidget* parent, const QString& caption, const QString& filter);

// Replaces the save dialog with a small name + format prompt. Returns
// /saved/<n>/<typed name> (empty on cancel) and stores the chosen filter row
// in *selected_filter so path_with_default_extension and the callers'
// suffix handling keep working unchanged.
[[nodiscard]] QString prompt_save_file(QWidget* parent, const QString& caption, const QString& initial_path,
                                       const QString& filter, QString* selected_filter);

// Hands an already-written MEMFS file to the browser as a download
// (Blob + <a download> click; no user-activation requirement, no cancel path).
void download_file_in_browser(const QString& path);

// Registers page-side dragover/drop listeners for files dragged in from the
// desktop. Qt 6.8 wasm has no external-drop support of its own (it never
// registers a browser dragover handler, so the browser can never deliver a
// drop to it). Dropped files are copied into MEMFS and reported one path per
// file through `open_dropped_path`. Call once at startup.
void install_web_drop_target(std::function<void(const QString& path)> open_dropped_path);

// Application-wide workaround for a Qt 6.8 wasm input bug: any top-level
// window opened from a secondary top-level window (any dialog, modal or not)
// is broken. The native combo popup renders but never receives pointer or
// key events, so it neither picks nor dismisses; a replacement chooser
// dialog (dialog-parented or parentless) received no input and did not even
// paint. Main-window popups, including the menus, work fine. The filter
// therefore intercepts every interaction that would open a dialog combo's
// popup and shows a popup-styled QListWidget as a plain child widget inside
// the dialog's own window, which paints and receives input normally. Picks
// emit the same activated/textActivated signals a real popup would; a press
// outside the list dismisses it like a popup. The same filter also embeds
// every QDialog that would open as a secondary-parented window (message
// boxes, color pickers, sub-dialogs of dialogs) as a centered child widget of
// the host window behind a click-blocking dim layer, because those windows
// are equally input-dead.
void install_wasm_dialog_combo_workaround();

// QMenu::exec replacement for menus opened from dialog contexts (flat menus
// only): shows the actions as an in-window list child of `host`, waits, and
// returns the picked action or null on dismissal.
[[nodiscard]] QAction* exec_menu_in_window(QMenu& menu, QPoint global_position, QWidget& host);

// Exported shim over dialog_utils.cpp's install_dialog_overflow_scroll so the
// embedded-dialog path can make an oversized dialog's button row reachable by
// scrolling. Declared here because this header is the wasm-only surface both
// TUs share. Returns true when the content was wrapped.
bool wrap_dialog_content_in_overflow_scroll(QDialog& dialog, QSize bound);

}  // namespace patchy::ui::wasm_files
