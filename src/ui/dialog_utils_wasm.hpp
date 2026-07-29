#pragma once

#include <QString>

#include <functional>

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

}  // namespace patchy::ui::wasm_files
