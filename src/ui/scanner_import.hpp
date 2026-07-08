#pragma once

#include <QString>

class QWidget;

namespace patchy::ui {

// WIA scanner/camera acquisition (Windows only; the menu action is #ifdef Q_OS_WIN-gated so
// no portable fallback implementation exists). Implemented in scanner_import_win.cpp per
// the AGENTS.md per-OS translation-unit convention.
enum class ScannerAcquireStatus {
  Acquired,   // file_path holds the scanned image (caller deletes it after import)
  Cancelled,  // the user closed the WIA dialog; stay silent
  NoDevice,   // no WIA-compatible scanner or camera is installed
  Failed,     // error holds a short description
};

struct ScannerAcquireResult {
  ScannerAcquireStatus status{ScannerAcquireStatus::Failed};
  QString file_path;
  QString error;
};

[[nodiscard]] ScannerAcquireResult acquire_image_from_scanner(QWidget* parent);

}  // namespace patchy::ui
