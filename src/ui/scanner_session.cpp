#include "ui/scanner_session.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

namespace {

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
// Moves a finished scan to `base_path` + its own extension (rename, or copy and remove
// across volumes). Returns the new path, or empty on failure.
QString move_scan_file(const QString& from, const QString& base_path) {
  const QString suffix = QFileInfo(from).suffix();
  const QString target = suffix.isEmpty() ? base_path : base_path + QLatin1Char('.') + suffix.toLower();
  QFile::remove(target);
  if (QFile::rename(from, target)) {
    return target;
  }
  if (QFile::copy(from, target)) {
    QFile::remove(from);
    return target;
  }
  return {};
}
#endif

// Test scanner: PATCHY_FAKE_BOOK_SCANNER lists image files (QDir::listSeparator between
// them) handed out one per scan. Each image is taken as a whole-bed scan at the
// requested resolution, so the requested area is cut from it the way a real driver
// would; PATCHY_FAKE_BOOK_SCANNER_BED ("8.5x11.7", inches) sets the reported bed.
// PATCHY_FAKE_BOOK_SCANNER_DEVICES ("id=Name;id2=Name2", or "none") sets the scanner list
// and PATCHY_FAKE_BOOK_SCANNER_DPIS ("150,300") the resolutions open() reports; changing
// them and calling notify_test_scanner_devices_changed() plays a scanner turning on or off.
class FakeScannerSession;
std::vector<FakeScannerSession*>& live_fake_sessions() {
  static std::vector<FakeScannerSession*> sessions;
  return sessions;
}

class FakeScannerSession final : public ScannerSession {
public:
  explicit FakeScannerSession(QStringList files) : files_(std::move(files)) {
    const QString bed = qEnvironmentVariable("PATCHY_FAKE_BOOK_SCANNER_BED", QStringLiteral("8.5x11.7"));
    const QStringList parts = bed.split(QLatin1Char('x'));
    if (parts.size() == 2) {
      bed_width_ = parts[0].toDouble();
      bed_height_ = parts[1].toDouble();
    }
    live_fake_sessions().push_back(this);
  }
  ~FakeScannerSession() override { std::erase(live_fake_sessions(), this); }

  QString backend_name() const override { return QStringLiteral("Test scanner"); }
  std::vector<ScannerDeviceInfo> devices() override {
    if (!qEnvironmentVariableIsSet("PATCHY_FAKE_BOOK_SCANNER_DEVICES")) {
      return {{QStringLiteral("fake"), QStringLiteral("Test scanner")}};
    }
    std::vector<ScannerDeviceInfo> found;
    const QString list = qEnvironmentVariable("PATCHY_FAKE_BOOK_SCANNER_DEVICES");
    for (const QString& entry : list.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
      const qsizetype equals = entry.indexOf(QLatin1Char('='));
      if (equals > 0) {
        found.push_back({entry.left(equals), entry.mid(equals + 1)});
      }
    }
    return found;
  }
  bool open(const QString& device_id, QString* error) override {
    const auto listed = devices();
    if (std::none_of(listed.begin(), listed.end(), [&](const ScannerDeviceInfo& d) { return d.id == device_id; })) {
      if (error != nullptr) {
        *error = QStringLiteral("The test scanner is switched off.");
      }
      return false;
    }
    resolutions_ = {150, 200, 300, 600};
    if (qEnvironmentVariableIsSet("PATCHY_FAKE_BOOK_SCANNER_DPIS")) {
      resolutions_.clear();
      for (const QString& dpi : qEnvironmentVariable("PATCHY_FAKE_BOOK_SCANNER_DPIS").split(QLatin1Char(','))) {
        if (dpi.toInt() > 0) {
          resolutions_.push_back(dpi.toInt());
        }
      }
    }
    open_device_ = device_id;
    return true;
  }
  void close() override { open_device_.clear(); }
  void notify_devices_changed() {
    // A real backend's handle dies when its scanner turns off or on.
    open_device_.clear();
    emit devices_changed(devices());
  }
  ScannerCapabilities capabilities() const override {
    ScannerCapabilities caps;
    caps.direct_control = true;
    caps.resolutions = resolutions_;
    caps.bed_width_inches = bed_width_;
    caps.bed_height_inches = bed_height_;
    caps.scan_area = true;
    return caps;
  }
  void start_scan(const ScanRequest& request) override {
    busy_ = true;
    QPointer<FakeScannerSession> self(this);
    QTimer::singleShot(0, this, [self, request]() {
      if (self) {
        self->finish(request);
      }
    });
  }
  void cancel() override {}
  bool busy() const override { return busy_; }

private:
  void finish(const ScanRequest& request) {
    ScanResult result;
    if (open_device_.isEmpty()) {
      result.status = ScannerAcquireStatus::Failed;
      result.device_lost = true;
      result.error = QStringLiteral("The test scanner is not open.");
    } else if (next_ >= files_.size()) {
      result.status = ScannerAcquireStatus::Failed;
      result.error = QStringLiteral("The test scanner has no more images.");
    } else {
      QImage image(files_[next_++]);
      if (image.isNull()) {
        result.error = QStringLiteral("The test scanner image could not be read.");
      } else {
        if (request.area_inches) {
          const QRectF& area = *request.area_inches;
          const QRect pixels(static_cast<int>(std::lround(area.x() * request.dpi)),
                             static_cast<int>(std::lround(area.y() * request.dpi)),
                             static_cast<int>(std::lround(area.width() * request.dpi)),
                             static_cast<int>(std::lround(area.height() * request.dpi)));
          image = image.copy(pixels.intersected(image.rect()));
        }
        image = image.convertToFormat(request.mode == ScanColorMode::Grayscale ? QImage::Format_Grayscale8
                                                                               : QImage::Format_RGB888);
        const QString path = request.output_base_path + QStringLiteral(".png");
        if (image.save(path, "PNG")) {
          result.status = ScannerAcquireStatus::Acquired;
          result.file_path = path;
          result.horizontal_dpi = request.dpi;
          result.vertical_dpi = request.dpi;
          result.area_applied = true;
        } else {
          result.error = QStringLiteral("The test scan could not be written.");
        }
      }
    }
    busy_ = false;
    emit progress(100);
    emit finished(result);
  }

  QStringList files_;
  QString open_device_;
  std::vector<int> resolutions_{150, 200, 300, 600};
  qsizetype next_{0};
  double bed_width_{8.5};
  double bed_height_{11.7};
  bool busy_{false};
};

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
// The system scan dialog once per scan: resolution, mode and area are the dialog's.
class DialogScannerSession final : public ScannerSession {
public:
  explicit DialogScannerSession(QWidget* parent) : parent_(parent) {}

  QString backend_name() const override {
    return QCoreApplication::translate("ScannerSession", "System scan dialog");
  }
  std::vector<ScannerDeviceInfo> devices() override {
    return {{QStringLiteral("system-dialog"), QCoreApplication::translate("ScannerSession", "Choose in the scan dialog")}};
  }
  bool open(const QString&, QString*) override { return true; }
  ScannerCapabilities capabilities() const override { return {}; }
  void start_scan(const ScanRequest& request) override {
    if (busy_) {
      return;
    }
    busy_ = true;
    QPointer<DialogScannerSession> self(this);
    const QString base = request.output_base_path;
#if defined(Q_OS_MACOS)
    acquire_image_from_scanner_async(parent_, [self, base](ScannerAcquireResult acquired) {
      if (self) {
        self->finish(acquired, base);
      }
    });
#else
    // Synchronous on Windows and Linux: return to the event loop first so the caller's
    // UI updates before the dialog appears.
    QTimer::singleShot(0, this, [self, base]() {
      if (!self) {
        return;
      }
      ScannerAcquireResult acquired = acquire_image_from_scanner(self->parent_);
      if (self) {
        self->finish(acquired, base);
      }
    });
#endif
  }
  void cancel() override {}
  bool busy() const override { return busy_; }

private:
  void finish(const ScannerAcquireResult& acquired, const QString& base) {
    ScanResult result;
    result.status = acquired.status;
    result.error = acquired.error;
    result.horizontal_dpi = acquired.horizontal_dpi;
    result.vertical_dpi = acquired.vertical_dpi;
    if (acquired.status == ScannerAcquireStatus::Acquired) {
      result.file_path = move_scan_file(acquired.file_path, base);
      if (result.file_path.isEmpty()) {
        result.status = ScannerAcquireStatus::Failed;
        result.error = QCoreApplication::translate("ScannerSession", "The scan could not be moved into the project folder.");
        QFile::remove(acquired.file_path);
      }
    }
    busy_ = false;
    emit finished(result);
  }

  QPointer<QWidget> parent_;
  bool busy_{false};
};
#endif

}  // namespace

bool scanner_sessions_available() {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
  return true;
#else
  return qEnvironmentVariableIsSet("PATCHY_FAKE_BOOK_SCANNER");
#endif
}

void notify_test_scanner_devices_changed() {
  const std::vector<FakeScannerSession*> sessions = live_fake_sessions();
  for (FakeScannerSession* session : sessions) {
    session->notify_devices_changed();
  }
}

std::unique_ptr<ScannerSession> create_dialog_scanner_session(QWidget* dialog_parent) {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
  return std::make_unique<DialogScannerSession>(dialog_parent);
#else
  Q_UNUSED(dialog_parent);
  return nullptr;
#endif
}

std::unique_ptr<ScannerSession> create_scanner_session(QWidget* dialog_parent) {
  if (qEnvironmentVariableIsSet("PATCHY_FAKE_BOOK_SCANNER")) {
    return std::make_unique<FakeScannerSession>(
        qEnvironmentVariable("PATCHY_FAKE_BOOK_SCANNER").split(QDir::listSeparator(), Qt::SkipEmptyParts));
  }
#if defined(Q_OS_WIN)
  Q_UNUSED(dialog_parent);
  return create_wia_scanner_session();
#else
  return create_dialog_scanner_session(dialog_parent);
#endif
}

}  // namespace patchy::ui
