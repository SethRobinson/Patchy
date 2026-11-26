#pragma once

#include "ui/scanner_import.hpp"

#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>

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
// Scans complete asynchronously through finished(); progress() reports percent. A backend
// that hears about scanners turning on or off (WIA) emits devices_changed() while the
// session exists; nothing listens before a session is created or after it is destroyed.
// A backend that can hear the scanner's own scan buttons (WIA) says so after open() and
// reports presses through scan_button_pressed() while it is asked to listen; a press is
// the user's own, and what it starts is the caller's decision (docs/book-scan-scanners.md, "The
// scanner's own Scan button"). No backend ever starts a scan by itself.

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
  // The scan failed because the open device went away (switched off, unplugged, or
  // power-cycled so its handle is dead): open() again before the next scan.
  bool device_lost{false};
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
  // Lets go of the open device without waiting; the next open() starts fresh.
  virtual void close() {}
  [[nodiscard]] virtual ScannerCapabilities capabilities() const = 0;
  // Starts a scan; exactly one finished() follows. Ignored while busy().
  virtual void start_scan(const ScanRequest& request) = 0;
  virtual void cancel() = 0;
  [[nodiscard]] virtual bool busy() const = 0;
  // True when the scanner opened last declares a scan button this backend can listen for.
  // It stays so after close(), until the next open(). False where the backend cannot tell.
  [[nodiscard]] virtual bool has_scan_button() const { return false; }
  // Starts or stops listening for that scanner's scan buttons. While listening, a press
  // is reported by scan_button_pressed() and does not reach the program Windows would
  // otherwise start; while not, the buttons do what they always did. Does nothing in a
  // backend without button events, and never fails loudly: a driver that refuses simply
  // reports no presses.
  virtual void set_scan_button_listening(bool listen) { Q_UNUSED(listen); }

signals:
  void progress(int percent);
  void status_message(const QString& text);
  void finished(const patchy::ui::ScanResult& result);
  // A scanner was switched on or off, plugged in or unplugged: `devices` is the list
  // now, read off the GUI thread. An open device may be dead after this (open again).
  void devices_changed(const std::vector<patchy::ui::ScannerDeviceInfo>& devices);
  // The user pressed a scan button on the scanner `device_id` while this session listens.
  void scan_button_pressed(const QString& device_id);
};

// The session for this platform: the fake scanner when PATCHY_FAKE_BOOK_SCANNER is set,
// else the direct backend where one exists, else the system dialog per scan.
// `dialog_parent` owns any system dialog a scan shows.
[[nodiscard]] std::unique_ptr<ScannerSession> create_scanner_session(QWidget* dialog_parent);
// The system-dialog session, for the user to choose when direct control fails.
[[nodiscard]] std::unique_ptr<ScannerSession> create_dialog_scanner_session(QWidget* dialog_parent);
// True when this build can scan at all (false on WebAssembly).
[[nodiscard]] bool scanner_sessions_available();
// Tests: every test scanner session re-reads PATCHY_FAKE_BOOK_SCANNER_DEVICES and emits
// devices_changed(), as a real backend does when a scanner turns on or off.
void notify_test_scanner_devices_changed();
// Tests: plays a press of the test scanner's scan button. The test scanner declares one
// when PATCHY_FAKE_BOOK_SCANNER_BUTTON is 1 and, like a real backend, reports the press
// only while it is asked to listen.
void press_test_scanner_button();

// The scanner log: which events the opened scanner declares, every button event that
// arrives and what became of it, so a button that does nothing can be told apart from a
// driver that sends nothing. The last lines are kept in memory; a direct backend also
// appends them to scanner_log_file_path(). Thread-safe.
void scanner_log(const QString& line);
[[nodiscard]] QStringList scanner_log_lines();
// `<per-user local data>/scanner-log.txt`.
[[nodiscard]] QString scanner_log_file_path();
// Starts writing the log to its file (a direct backend calls this once; the file starts
// over when it has grown past a few hundred kilobytes). Call from the GUI thread.
void start_scanner_log_file();

#if defined(Q_OS_WIN)
[[nodiscard]] std::unique_ptr<ScannerSession> create_wia_scanner_session();
#endif

}  // namespace patchy::ui

Q_DECLARE_METATYPE(patchy::ui::ScanResult)
