#pragma once

#include <QString>
#include <functional>

class QWidget;

namespace patchy::ui {

// Native scanner acquisition. Windows uses WIA and also exposes cameras; macOS uses
// ImageKit/ImageCaptureCore and exposes scanners only. Implemented in per-OS translation
// units following the docs/platform.md convention.
enum class ScannerAcquireStatus {
  Acquired,   // file_path holds the scanned image (caller deletes it after import)
  Cancelled,  // the user closed the native acquisition UI; stay silent
  NoDevice,   // the native backend reported that no compatible device is available
  Failed,     // error holds a short description
};

struct ScannerAcquireResult {
  ScannerAcquireStatus status{ScannerAcquireStatus::Failed};
  QString file_path;
  QString error;
};

#ifdef Q_OS_WIN
[[nodiscard]] ScannerAcquireResult acquire_image_from_scanner(QWidget* parent);
#elif defined(Q_OS_MACOS)
// AppKit sheets must return to the application's main run loop before they can receive
// input, so macOS acquisition completes through this callback instead of a nested Qt loop.
using ScannerAcquireCallback = std::function<void(ScannerAcquireResult)>;
void acquire_image_from_scanner_async(QWidget* parent, ScannerAcquireCallback callback);
#endif

}  // namespace patchy::ui
