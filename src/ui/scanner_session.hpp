#pragma once

#include "ui/scanner_import.hpp"

#include <QObject>
#include <QRectF>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// Repeated scans with fixed settings, for Scan a Book to PDF (docs/book-scan.md). Unlike
// acquire_image_from_scanner, which shows the system scan UI every time, a session opens
// one device and scans with the settings it is given, so a book of 150 spreads needs one
// key press per spread. Backends:
//   - Windows: WIA 2.0 driven directly (scanner_session_win.cpp).
//   - Every desktop platform: the system scan dialog once per scan (the fallback, and
//     macOS/Linux until their direct backends land), where settings are the user's.
//   - Tests: PATCHY_FAKE_BOOK_SCANNER, a queue of image files (scanner_session.cpp).
// Scans complete asynchronously through finished(); progress() reports percent.

struct ScannerDeviceInfo {
  QString id;
  QString name;
};

enum class ScanColorMode { Color, Grayscale };

struct ScannerCapabilities {
  // False when every scan goes through the system dialog: resolution, mode and area
  // are then the dialog's, not the request's.
  bool direct_control{false};
  std::vector<int> resolutions;  // empty = unknown
  double bed_width_inches{0.0};  // 0 = unknown
  double bed_height_inches{0.0};
  bool scan_area{false};  // the request's area is honored
};

struct ScanRequest {
  int dpi{300};
  ScanColorMode mode{ScanColorMode::Color};
  // Inches from the bed's registration corner; unset scans the whole bed.
  std::optional<QRectF> area_inches;
  // Where the scan goes, without extension; the backend adds the extension it writes.
  QString output_base_path;
};

struct ScanResult {
  ScannerAcquireStatus status{ScannerAcquireStatus::Failed};
  QString file_path;
  QString error;
  double horizontal_dpi{0.0};
  double vertical_dpi{0.0};
  // True when the image covers only the requested area. False means the whole bed came
  // back (system dialog, or a driver that ignored the area) and the caller crops.
  bool area_applied{false};
};

class ScannerSession : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;
  ~ScannerSession() override = default;

  // A short label for the backend ("Windows Image Acquisition", "System scan dialog").
  [[nodiscard]] virtual QString backend_name() const = 0;
  // Lists scanners. Blocks briefly.
  [[nodiscard]] virtual std::vector<ScannerDeviceInfo> devices() = 0;
  // Opens one device for the session's following scans; false fills `error`.
  virtual bool open(const QString& device_id, QString* error) = 0;
  [[nodiscard]] virtual ScannerCapabilities capabilities() const = 0;
  // Starts a scan; exactly one finished() follows. Ignored while busy().
  virtual void start_scan(const ScanRequest& request) = 0;
  virtual void cancel() = 0;
  [[nodiscard]] virtual bool busy() const = 0;

signals:
  void progress(int percent);
  void status_message(const QString& text);
  void finished(const patchy::ui::ScanResult& result);
};

// The session for this platform: the fake scanner when PATCHY_FAKE_BOOK_SCANNER is set,
// else the direct backend where one exists, else the system dialog per scan.
// `dialog_parent` owns any system dialog a scan shows.
[[nodiscard]] std::unique_ptr<ScannerSession> create_scanner_session(QWidget* dialog_parent);
// The system-dialog session, for the user to choose when direct control fails.
[[nodiscard]] std::unique_ptr<ScannerSession> create_dialog_scanner_session(QWidget* dialog_parent);
// True when this build can scan at all (false on WebAssembly).
[[nodiscard]] bool scanner_sessions_available();

#if defined(Q_OS_WIN)
[[nodiscard]] std::unique_ptr<ScannerSession> create_wia_scanner_session();
#endif

}  // namespace patchy::ui

Q_DECLARE_METATYPE(patchy::ui::ScanResult)
