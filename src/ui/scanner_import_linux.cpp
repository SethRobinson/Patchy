// Linux scanner and camera acquisition (docs/import.md): SANE for scanners, libgphoto2 for
// cameras. Both libraries are loaded with dlopen at run time like fontconfig and libheif, so
// the build needs no development packages and the AppImage bundles neither: the user's
// distro supplies the drivers. The C ABI mirrored below is the stable public subset of
// SANE 1 (sane.h, unchanged since 1.0) and libgphoto2 2.5 (opaque handles only; no struct
// is passed by value).
//
// Linux has no system acquisition UI, so Patchy shows its own small dialog: a device list
// (scanners and cameras together, like Windows' WIA picker), resolution/mode/source for a
// scanner, and the image list for a camera. Every library call runs on a worker thread,
// one task at a time; the dialog waits for that worker before it closes.

#include "ui/scanner_import.hpp"

#include "support/scan_frame.hpp"
#include "ui/dialog_utils.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QImageWriter>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace patchy::ui {
namespace {

// ---------------------------------------------------------------------------------------
// SANE 1 C ABI.

using SaneWord = int;
using SaneHandle = void*;
constexpr int kSaneStatusGood = 0;
constexpr int kSaneStatusCancelled = 2;
constexpr int kSaneStatusEof = 5;
constexpr int kSaneTypeInt = 1;
constexpr int kSaneTypeFixed = 2;
constexpr int kSaneTypeString = 3;
constexpr int kSaneConstraintRange = 1;
constexpr int kSaneConstraintWordList = 2;
constexpr int kSaneConstraintStringList = 3;
constexpr int kSaneActionGetValue = 0;
constexpr int kSaneActionSetValue = 1;
constexpr int kSaneCapSoftSelect = 1;
constexpr int kSaneCapInactive = 32;

struct SaneDevice {
  const char* name;
  const char* vendor;
  const char* model;
  const char* type;
};
struct SaneRange {
  SaneWord min;
  SaneWord max;
  SaneWord quant;
};
struct SaneOptionDescriptor {
  const char* name;
  const char* title;
  const char* desc;
  int type;
  int unit;
  int size;
  int cap;
  int constraint_type;
  union {
    const char* const* string_list;
    const SaneWord* word_list;
    const SaneRange* range;
  } constraint;
};
struct SaneParameters {
  int format;
  int last_frame;
  int bytes_per_line;
  int pixels_per_line;
  int lines;
  int depth;
};

struct SaneLibrary {
  bool loaded{false};
  int (*init)(int*, void*) = nullptr;
  void (*exit)() = nullptr;
  int (*get_devices)(const SaneDevice***, int) = nullptr;
  int (*open)(const char*, SaneHandle*) = nullptr;
  void (*close)(SaneHandle) = nullptr;
  const SaneOptionDescriptor* (*get_option_descriptor)(SaneHandle, int) = nullptr;
  int (*control_option)(SaneHandle, int, int, void*, int*) = nullptr;
  int (*get_parameters)(SaneHandle, SaneParameters*) = nullptr;
  int (*start)(SaneHandle) = nullptr;
  int (*read)(SaneHandle, unsigned char*, int, int*) = nullptr;
  void (*cancel)(SaneHandle) = nullptr;
  const char* (*strstatus)(int) = nullptr;
};

// ---------------------------------------------------------------------------------------
// libgphoto2 2.5 C ABI (opaque handles).

struct GPContext;
struct CameraList;
struct Camera;
struct CameraFile;
struct GPPortInfoListOpaque;
using GPPortInfo = void*;
constexpr int kGpOk = 0;
constexpr int kGpFileTypeNormal = 1;

struct GphotoLibrary {
  bool loaded{false};
  GPContext* (*context_new)() = nullptr;
  void (*context_unref)(GPContext*) = nullptr;
  int (*list_new)(CameraList**) = nullptr;
  int (*list_free)(CameraList*) = nullptr;
  int (*list_count)(CameraList*) = nullptr;
  int (*list_get_name)(CameraList*, int, const char**) = nullptr;
  int (*list_get_value)(CameraList*, int, const char**) = nullptr;
  int (*camera_autodetect)(CameraList*, GPContext*) = nullptr;
  int (*camera_new)(Camera**) = nullptr;
  int (*camera_init)(Camera*, GPContext*) = nullptr;
  int (*camera_exit)(Camera*, GPContext*) = nullptr;
  int (*camera_unref)(Camera*) = nullptr;
  int (*camera_set_port_info)(Camera*, GPPortInfo) = nullptr;
  int (*port_info_list_new)(GPPortInfoListOpaque**) = nullptr;
  int (*port_info_list_load)(GPPortInfoListOpaque*) = nullptr;
  int (*port_info_list_lookup_path)(GPPortInfoListOpaque*, const char*) = nullptr;
  int (*port_info_list_get_info)(GPPortInfoListOpaque*, int, GPPortInfo*) = nullptr;
  int (*port_info_list_free)(GPPortInfoListOpaque*) = nullptr;
  int (*folder_list_files)(Camera*, const char*, CameraList*, GPContext*) = nullptr;
  int (*folder_list_folders)(Camera*, const char*, CameraList*, GPContext*) = nullptr;
  int (*file_new)(CameraFile**) = nullptr;
  int (*file_unref)(CameraFile*) = nullptr;
  int (*file_get)(Camera*, const char*, const char*, int, CameraFile*, GPContext*) = nullptr;
  int (*file_get_data_and_size)(CameraFile*, const char**, unsigned long*) = nullptr;
  const char* (*result_as_string)(int) = nullptr;
};

template <typename Function>
bool resolve(void* handle, const char* name, Function& target) {
  target = reinterpret_cast<Function>(dlsym(handle, name));
  return target != nullptr;
}

void* open_first(std::initializer_list<const char*> candidates) {
  for (const char* candidate : candidates) {
    if (void* handle = dlopen(candidate, RTLD_NOW | RTLD_LOCAL); handle != nullptr) {
      return handle;
    }
  }
  return nullptr;
}

// Loaded once and never closed: SANE backends register atexit handlers in some distros.
const SaneLibrary& sane_library() {
  static SaneLibrary library;
  static std::once_flag once;
  std::call_once(once, [] {
    void* handle = open_first({"libsane.so.1", "libsane.so"});
    if (handle == nullptr) {
      return;
    }
    auto& l = library;
    l.loaded = resolve(handle, "sane_init", l.init) && resolve(handle, "sane_exit", l.exit) &&
               resolve(handle, "sane_get_devices", l.get_devices) && resolve(handle, "sane_open", l.open) &&
               resolve(handle, "sane_close", l.close) &&
               resolve(handle, "sane_get_option_descriptor", l.get_option_descriptor) &&
               resolve(handle, "sane_control_option", l.control_option) &&
               resolve(handle, "sane_get_parameters", l.get_parameters) && resolve(handle, "sane_start", l.start) &&
               resolve(handle, "sane_read", l.read) && resolve(handle, "sane_cancel", l.cancel) &&
               resolve(handle, "sane_strstatus", l.strstatus);
  });
  return library;
}

const GphotoLibrary& gphoto_library() {
  static GphotoLibrary library;
  static std::once_flag once;
  std::call_once(once, [] {
    void* handle = open_first({"libgphoto2.so.6", "libgphoto2.so"});
    if (handle == nullptr) {
      return;
    }
    auto& l = library;
    l.loaded = resolve(handle, "gp_context_new", l.context_new) &&
               resolve(handle, "gp_context_unref", l.context_unref) && resolve(handle, "gp_list_new", l.list_new) &&
               resolve(handle, "gp_list_free", l.list_free) && resolve(handle, "gp_list_count", l.list_count) &&
               resolve(handle, "gp_list_get_name", l.list_get_name) &&
               resolve(handle, "gp_list_get_value", l.list_get_value) &&
               resolve(handle, "gp_camera_autodetect", l.camera_autodetect) &&
               resolve(handle, "gp_camera_new", l.camera_new) && resolve(handle, "gp_camera_init", l.camera_init) &&
               resolve(handle, "gp_camera_exit", l.camera_exit) &&
               resolve(handle, "gp_camera_unref", l.camera_unref) &&
               resolve(handle, "gp_camera_set_port_info", l.camera_set_port_info) &&
               resolve(handle, "gp_port_info_list_new", l.port_info_list_new) &&
               resolve(handle, "gp_port_info_list_load", l.port_info_list_load) &&
               resolve(handle, "gp_port_info_list_lookup_path", l.port_info_list_lookup_path) &&
               resolve(handle, "gp_port_info_list_get_info", l.port_info_list_get_info) &&
               resolve(handle, "gp_port_info_list_free", l.port_info_list_free) &&
               resolve(handle, "gp_camera_folder_list_files", l.folder_list_files) &&
               resolve(handle, "gp_camera_folder_list_folders", l.folder_list_folders) &&
               resolve(handle, "gp_file_new", l.file_new) && resolve(handle, "gp_file_unref", l.file_unref) &&
               resolve(handle, "gp_camera_file_get", l.file_get) &&
               resolve(handle, "gp_file_get_data_and_size", l.file_get_data_and_size) &&
               resolve(handle, "gp_result_as_string", l.result_as_string);
  });
  return library;
}

QString sane_status_text(int status) {
  const char* text = sane_library().strstatus != nullptr ? sane_library().strstatus(status) : nullptr;
  return text != nullptr ? QString::fromUtf8(text) : QString::number(status);
}

QString gphoto_status_text(int status) {
  const char* text = gphoto_library().result_as_string != nullptr ? gphoto_library().result_as_string(status) : nullptr;
  return text != nullptr ? QString::fromUtf8(text) : QString::number(status);
}

// What the user can do about a failure; empty when the cause is unknown, so the library's
// own text stays the whole message.
QString failure_explanation(support::AcquireFailureCause cause, const QString& address) {
  using Cause = support::AcquireFailureCause;
  switch (cause) {
    case Cause::NetworkUnreachable:
      return (address.isEmpty()
                  ? QCoreApplication::translate("ScannerImport", "The network scanner does not answer.")
                  : QCoreApplication::translate("ScannerImport", "The network scanner at %1 does not answer.")
                        .arg(address)) +
             QLatin1Char(' ') +
             QCoreApplication::translate(
                 "ScannerImport",
                 "Check that it is switched on and connected to the same network as this computer. If its address "
                 "changed, correct or remove the old address in the scanner configuration in /etc/sane.d "
                 "(airscan.conf or escl.conf).");
    case Cause::ConnectionLost:
      return QCoreApplication::translate(
          "ScannerImport",
          "The device stopped responding. Check that it is switched on, the cable is connected and it is not in "
          "sleep mode, then try again.");
    case Cause::Busy:
      return QCoreApplication::translate(
          "ScannerImport",
          "The scanner is in use by another program or still finishing a previous job. Close the other program or "
          "wait a moment, then try again.");
    case Cause::AccessDenied:
      return QCoreApplication::translate(
          "ScannerImport",
          "This user account is not allowed to use the device. Add your account to the scanner group (often "
          "\"scanner\" or \"lp\"), or install the device rules from your distribution's SANE or libgphoto2 package, "
          "then log in again.");
    case Cause::Jammed:
      return QCoreApplication::translate("ScannerImport", "Paper is jammed in the scanner. Clear the jam and try again.");
    case Cause::FeederEmpty:
      return QCoreApplication::translate(
          "ScannerImport", "The document feeder is empty. Load the pages, or choose the flatbed as the source.");
    case Cause::CoverOpen:
      return QCoreApplication::translate("ScannerImport", "The scanner cover is open. Close it and try again.");
    case Cause::UnsupportedSettings:
      return QCoreApplication::translate(
          "ScannerImport", "The scanner does not accept these settings. Try another mode, resolution or source.");
    case Cause::OutOfMemory:
      return QCoreApplication::translate(
          "ScannerImport", "There is not enough memory for this scan. Try a lower resolution.");
    case Cause::CameraClaimed:
      return QCoreApplication::translate(
          "ScannerImport",
          "Another program is using the camera, often the file manager, which opens cameras automatically. Eject "
          "or unmount the camera there, then try again.");
    case Cause::CameraNotSupported:
      return QCoreApplication::translate(
          "ScannerImport",
          "libgphoto2 does not support this camera model. Copy the images with a card reader or the file manager "
          "instead.");
    case Cause::CorruptedData:
      return QCoreApplication::translate(
          "ScannerImport", "The camera sent damaged data. Reconnect the camera and try again.");
    case Cause::ImageMissing:
      return QCoreApplication::translate(
          "ScannerImport", "The image is no longer on the camera. Pick the device again to refresh the list.");
    case Cause::Unknown:
      break;
  }
  return {};
}

// "<what failed> (<library text>)" followed by the explanation, when there is one.
QString describe_failure(const QString& headline, support::AcquireFailureCause cause, const QString& address = {}) {
  const auto explanation = failure_explanation(cause, address);
  return explanation.isEmpty() ? headline : headline + QStringLiteral("\n\n") + explanation;
}

// ---------------------------------------------------------------------------------------
// Session state shared between the dialog (main thread) and its worker. Library calls only
// happen on the worker, one task at a time, except sane_cancel, which SANE allows from
// any thread while a read is blocked.

struct DeviceEntry {
  bool camera{false};
  QString id;  // SANE device name, or gphoto2 port path
  QString label;
  QString address;  // network scanners: the address the backend reports, for error messages
};

struct ScannerSettings {
  std::vector<int> resolutions;
  int resolution{0};
  QStringList modes;
  QString mode;
  QStringList sources;
  QString source;
};

struct CameraImage {
  QString folder;
  QString name;
};

struct Session {
  bool sane_initialized{false};
  SaneHandle scanner{nullptr};
  QString scanner_name;     // SANE device name of the open scanner
  QString scanner_address;  // its network address, when the backend reports one
  GPContext* gphoto_context{nullptr};
  Camera* camera{nullptr};
  std::atomic<SaneHandle> active_scan{nullptr};  // set only while run_scan reads
  std::atomic<bool> cancel_requested{false};
  std::atomic<int> progress_percent{-1};

  ~Session() { close_all(); }

  void close_device() {
    if (scanner != nullptr) {
      sane_library().close(scanner);
      scanner = nullptr;
    }
    scanner_name.clear();
    scanner_address.clear();
    if (camera != nullptr) {
      gphoto_library().camera_exit(camera, gphoto_context);
      gphoto_library().camera_unref(camera);
      camera = nullptr;
    }
  }

  void close_all() {
    close_device();
    if (gphoto_context != nullptr) {
      gphoto_library().context_unref(gphoto_context);
      gphoto_context = nullptr;
    }
    if (sane_initialized) {
      sane_library().exit();
      sane_initialized = false;
    }
  }
};

std::vector<DeviceEntry> enumerate_devices(Session& session) {
  std::vector<DeviceEntry> devices;
  if (sane_library().loaded) {
    int version = 0;
    if (sane_library().init(&version, nullptr) == kSaneStatusGood) {
      session.sane_initialized = true;
      const SaneDevice** list = nullptr;
      if (sane_library().get_devices(&list, 0) == kSaneStatusGood && list != nullptr) {
        for (int i = 0; list[i] != nullptr; ++i) {
          const auto* device = list[i];
          const auto vendor = QString::fromUtf8(device->vendor != nullptr ? device->vendor : "").trimmed();
          const auto model = QString::fromUtf8(device->model != nullptr ? device->model : "").trimmed();
          auto name = QStringList{vendor, model}.join(QLatin1Char(' ')).trimmed();
          if (name.isEmpty()) {
            name = QString::fromUtf8(device->name);
          }
          const auto address = QString::fromStdString(
              support::network_scanner_address(device->model != nullptr ? device->model : ""));
          devices.push_back({false, QString::fromUtf8(device->name),
                             QCoreApplication::translate("ScannerImport", "Scanner: %1").arg(name), address});
        }
      }
    }
  }
  if (gphoto_library().loaded) {
    session.gphoto_context = gphoto_library().context_new();
    CameraList* list = nullptr;
    if (session.gphoto_context != nullptr && gphoto_library().list_new(&list) == kGpOk) {
      if (gphoto_library().camera_autodetect(list, session.gphoto_context) >= kGpOk) {
        const int count = gphoto_library().list_count(list);
        for (int i = 0; i < count; ++i) {
          const char* model = nullptr;
          const char* port = nullptr;
          gphoto_library().list_get_name(list, i, &model);
          gphoto_library().list_get_value(list, i, &port);
          if (port == nullptr || model == nullptr) {
            continue;
          }
          devices.push_back({true, QString::fromUtf8(port),
                             QCoreApplication::translate("ScannerImport", "Camera: %1")
                                 .arg(QString::fromUtf8(model)),
                             QString{}});
        }
      }
      gphoto_library().list_free(list);
    }
  }
  return devices;
}

int find_option(SaneHandle handle, const char* name, const SaneOptionDescriptor** descriptor_out) {
  SaneWord count = 0;
  if (sane_library().control_option(handle, 0, kSaneActionGetValue, &count, nullptr) != kSaneStatusGood) {
    return -1;
  }
  for (int i = 1; i < count; ++i) {
    const auto* descriptor = sane_library().get_option_descriptor(handle, i);
    if (descriptor != nullptr && descriptor->name != nullptr && std::strcmp(descriptor->name, name) == 0) {
      if (descriptor_out != nullptr) {
        *descriptor_out = descriptor;
      }
      return i;
    }
  }
  return -1;
}

bool option_settable(const SaneOptionDescriptor* descriptor) {
  return descriptor != nullptr && (descriptor->cap & kSaneCapSoftSelect) != 0 &&
         (descriptor->cap & kSaneCapInactive) == 0;
}

std::optional<QString> read_string_option(SaneHandle handle, int index, const SaneOptionDescriptor* descriptor) {
  if (descriptor->type != kSaneTypeString || descriptor->size <= 0) {
    return std::nullopt;
  }
  std::vector<char> buffer(static_cast<std::size_t>(descriptor->size) + 1, '\0');
  if (sane_library().control_option(handle, index, kSaneActionGetValue, buffer.data(), nullptr) != kSaneStatusGood) {
    return std::nullopt;
  }
  return QString::fromUtf8(buffer.data());
}

void read_string_choices(SaneHandle handle, const char* name, QStringList& choices, QString& current) {
  const SaneOptionDescriptor* descriptor = nullptr;
  const int index = find_option(handle, name, &descriptor);
  if (index < 0 || !option_settable(descriptor) || descriptor->constraint_type != kSaneConstraintStringList ||
      descriptor->constraint.string_list == nullptr) {
    return;
  }
  for (int i = 0; descriptor->constraint.string_list[i] != nullptr; ++i) {
    choices.push_back(QString::fromUtf8(descriptor->constraint.string_list[i]));
  }
  current = read_string_option(handle, index, descriptor).value_or(QString());
}

double word_to_dpi(SaneWord word, int type) {
  return support::sane_resolution_to_dpi(word, type == kSaneTypeFixed);
}

// The "resolution" option, when it is one SANE_Int or SANE_Fixed word: get and set copy
// `size` bytes, so a vector-valued option would overrun the single word read here.
int find_resolution_option(SaneHandle handle, const SaneOptionDescriptor** descriptor_out) {
  const SaneOptionDescriptor* descriptor = nullptr;
  const int index = find_option(handle, "resolution", &descriptor);
  if (index < 0 || descriptor == nullptr || (descriptor->type != kSaneTypeInt && descriptor->type != kSaneTypeFixed) ||
      descriptor->size != static_cast<int>(sizeof(SaneWord))) {
    return -1;
  }
  *descriptor_out = descriptor;
  return index;
}

void read_resolution_choices(SaneHandle handle, ScannerSettings& settings) {
  const SaneOptionDescriptor* descriptor = nullptr;
  const int index = find_resolution_option(handle, &descriptor);
  if (index < 0) {
    return;
  }
  SaneWord current = 0;
  sane_library().control_option(handle, index, kSaneActionGetValue, &current, nullptr);
  const int current_dpi = static_cast<int>(word_to_dpi(current, descriptor->type) + 0.5);
  std::vector<int> values;
  if (descriptor->constraint_type == kSaneConstraintWordList && descriptor->constraint.word_list != nullptr) {
    const int count = descriptor->constraint.word_list[0];
    for (int i = 1; i <= count; ++i) {
      values.push_back(static_cast<int>(word_to_dpi(descriptor->constraint.word_list[i], descriptor->type) + 0.5));
    }
  } else {
    static constexpr std::array<int, 10> kCommon{75, 100, 150, 200, 300, 400, 600, 1200, 2400, 4800};
    int low = 1;
    int high = 4800;
    int quant = 0;
    if (descriptor->constraint_type == kSaneConstraintRange && descriptor->constraint.range != nullptr) {
      low = static_cast<int>(word_to_dpi(descriptor->constraint.range->min, descriptor->type) + 0.5);
      high = static_cast<int>(word_to_dpi(descriptor->constraint.range->max, descriptor->type) + 0.5);
      quant = static_cast<int>(word_to_dpi(descriptor->constraint.range->quant, descriptor->type) + 0.5);
      values.push_back(low);
      values.push_back(high);
    }
    for (const int value : kCommon) {
      if (value >= low && value <= high && (quant <= 1 || (value - low) % quant == 0)) {
        values.push_back(value);
      }
    }
  }
  if (current_dpi > 0) {
    values.push_back(current_dpi);
  }
  values.erase(std::remove_if(values.begin(), values.end(), [](int value) { return value <= 0; }), values.end());
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  settings.resolutions = values;
  settings.resolution = std::find(values.begin(), values.end(), 300) != values.end() ? 300 : current_dpi;
}

struct OpenResult {
  QString error;
  ScannerSettings scanner;
  std::vector<CameraImage> camera_images;
};

void list_camera_folder(Session& session, const QString& folder, int depth, std::vector<CameraImage>& images) {
  constexpr int kMaxDepth = 8;
  constexpr std::size_t kMaxImages = 20000;
  if (depth > kMaxDepth || images.size() >= kMaxImages || session.cancel_requested) {
    return;
  }
  const auto& gp = gphoto_library();
  const auto folder_bytes = folder.toUtf8();
  CameraList* files = nullptr;
  if (gp.list_new(&files) == kGpOk) {
    if (gp.folder_list_files(session.camera, folder_bytes.constData(), files, session.gphoto_context) >= kGpOk) {
      const int count = gp.list_count(files);
      for (int i = 0; i < count && images.size() < kMaxImages; ++i) {
        const char* name = nullptr;
        if (gp.list_get_name(files, i, &name) >= kGpOk && name != nullptr) {
          images.push_back({folder, QString::fromUtf8(name)});
        }
      }
    }
    gp.list_free(files);
  }
  CameraList* folders = nullptr;
  if (gp.list_new(&folders) == kGpOk) {
    if (gp.folder_list_folders(session.camera, folder_bytes.constData(), folders, session.gphoto_context) >= kGpOk) {
      QStringList children;
      const int count = gp.list_count(folders);
      for (int i = 0; i < count; ++i) {
        const char* name = nullptr;
        if (gp.list_get_name(folders, i, &name) >= kGpOk && name != nullptr) {
          children.push_back(QString::fromUtf8(name));
        }
      }
      gp.list_free(folders);
      for (const auto& child : children) {
        const auto path = folder.endsWith(QLatin1Char('/')) ? folder + child : folder + QLatin1Char('/') + child;
        list_camera_folder(session, path, depth + 1, images);
      }
    } else {
      gp.list_free(folders);
    }
  }
}

OpenResult open_device(Session& session, const DeviceEntry& device) {
  OpenResult result;
  session.close_device();
  if (!device.camera) {
    const auto name = device.id.toUtf8();
    const int status = sane_library().open(name.constData(), &session.scanner);
    if (status != kSaneStatusGood || session.scanner == nullptr) {
      session.scanner = nullptr;
      result.error = describe_failure(
          QCoreApplication::translate("ScannerImport", "The scanner could not be opened (%1)").arg(sane_status_text(status)),
          support::classify_sane_failure(status, name.constData()), device.address);
      return result;
    }
    session.scanner_name = device.id;
    session.scanner_address = device.address;
    read_resolution_choices(session.scanner, result.scanner);
    read_string_choices(session.scanner, "mode", result.scanner.modes, result.scanner.mode);
    read_string_choices(session.scanner, "source", result.scanner.sources, result.scanner.source);
    if (result.scanner.modes.contains(QStringLiteral("Color"))) {
      result.scanner.mode = QStringLiteral("Color");
    }
    return result;
  }

  const auto& gp = gphoto_library();
  int status = gp.camera_new(&session.camera);
  GPPortInfoListOpaque* ports = nullptr;
  if (status >= kGpOk) {
    status = gp.port_info_list_new(&ports);
  }
  if (status >= kGpOk) {
    status = gp.port_info_list_load(ports);
  }
  if (status >= kGpOk) {
    const auto port = device.id.toUtf8();
    const int index = gp.port_info_list_lookup_path(ports, port.constData());
    GPPortInfo info = nullptr;
    status = index >= kGpOk ? gp.port_info_list_get_info(ports, index, &info) : index;
    if (status >= kGpOk) {
      status = gp.camera_set_port_info(session.camera, info);
    }
  }
  if (status >= kGpOk) {
    // With only the port set, gp_camera_init detects the model on that port.
    status = gp.camera_init(session.camera, session.gphoto_context);
  }
  if (ports != nullptr) {
    gp.port_info_list_free(ports);
  }
  if (status < kGpOk) {
    if (session.camera != nullptr) {
      gp.camera_unref(session.camera);
      session.camera = nullptr;
    }
    result.error = describe_failure(
        QCoreApplication::translate("ScannerImport", "The camera could not be opened (%1)").arg(gphoto_status_text(status)),
        support::classify_gphoto_failure(status));
    return result;
  }
  list_camera_folder(session, QStringLiteral("/"), 0, result.camera_images);
  return result;
}

void set_string_option(SaneHandle handle, const char* name, const QString& value) {
  const SaneOptionDescriptor* descriptor = nullptr;
  const int index = find_option(handle, name, &descriptor);
  if (index < 0 || !option_settable(descriptor) || descriptor->type != kSaneTypeString || descriptor->size <= 0) {
    return;
  }
  std::vector<char> buffer(static_cast<std::size_t>(descriptor->size), '\0');
  const auto bytes = value.toUtf8();
  std::memcpy(buffer.data(), bytes.constData(),
              std::min(buffer.size() - 1, static_cast<std::size_t>(bytes.size())));
  sane_library().control_option(handle, index, kSaneActionSetValue, buffer.data(), nullptr);
}

void set_resolution_option(SaneHandle handle, int dpi) {
  const SaneOptionDescriptor* descriptor = nullptr;
  const int index = find_resolution_option(handle, &descriptor);
  if (index < 0 || !option_settable(descriptor) || dpi <= 0) {
    return;
  }
  // SANE_Fixed is 16.16: keep the shifted value inside a signed word.
  const SaneWord whole = static_cast<SaneWord>(std::min(dpi, 32767));
  SaneWord word = descriptor->type == kSaneTypeFixed ? whole * 65536 : static_cast<SaneWord>(dpi);
  sane_library().control_option(handle, index, kSaneActionSetValue, &word, nullptr);
}

double read_resolution_dpi(SaneHandle handle) {
  const SaneOptionDescriptor* descriptor = nullptr;
  const int index = find_resolution_option(handle, &descriptor);
  if (index < 0) {
    return 0.0;
  }
  SaneWord word = 0;
  if (sane_library().control_option(handle, index, kSaneActionGetValue, &word, nullptr) != kSaneStatusGood) {
    return 0.0;
  }
  return word_to_dpi(word, descriptor->type);
}

QImage image_from_scan(const support::ScanImage& scan) {
  QImage::Format format = QImage::Format_Invalid;
  if (scan.channels == 1) {
    format = scan.bit_depth == 16 ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8;
  } else {
    format = scan.bit_depth == 16 ? QImage::Format_RGBX64 : QImage::Format_RGB888;
  }
  QImage image(scan.width, scan.height, format);
  if (image.isNull()) {
    return image;
  }
  const auto sample_bytes = static_cast<std::size_t>(scan.bit_depth / 8);
  const auto row_bytes = static_cast<std::size_t>(scan.width) * static_cast<std::size_t>(scan.channels) * sample_bytes;
  for (int y = 0; y < scan.height; ++y) {
    const auto* source = scan.samples.data() + static_cast<std::size_t>(y) * row_bytes;
    auto* target = image.scanLine(y);
    if (format == QImage::Format_RGBX64) {
      auto* out = reinterpret_cast<quint16*>(target);
      for (int x = 0; x < scan.width; ++x) {
        std::memcpy(out + static_cast<std::ptrdiff_t>(x) * 4, source + static_cast<std::size_t>(x) * 6, 6);
        out[static_cast<std::ptrdiff_t>(x) * 4 + 3] = 0xFFFF;
      }
    } else {
      std::memcpy(target, source, row_bytes);
    }
  }
  return image;
}

QString temporary_scan_path(const QString& suffix) {
  const auto folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (folder.isEmpty() || !QDir().mkpath(folder)) {
    return {};
  }
  return QDir(folder).filePath(QStringLiteral("patchy-scan-%1-%2.%3")
                                   .arg(QCoreApplication::applicationPid())
                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces), suffix));
}

ScannerAcquireResult failure(const QString& error) {
  ScannerAcquireResult result;
  result.status = ScannerAcquireStatus::Failed;
  result.error = error;
  return result;
}

QString scan_failure(const Session& session, int status) {
  return describe_failure(
      QCoreApplication::translate("ScannerImport", "The scan could not be completed (%1)").arg(sane_status_text(status)),
      support::classify_sane_failure(status, session.scanner_name.toStdString()), session.scanner_address);
}

ScannerAcquireResult run_scan(Session& session, const ScannerSettings& settings) {
  auto* handle = session.scanner;
  session.active_scan = handle;
  const auto clear_active = std::unique_ptr<Session, void (*)(Session*)>(
      &session, [](Session* owner) { owner->active_scan = nullptr; });
  if (!settings.mode.isEmpty()) {
    set_string_option(handle, "mode", settings.mode);
  }
  if (!settings.source.isEmpty()) {
    set_string_option(handle, "source", settings.source);
  }
  set_resolution_option(handle, settings.resolution);

  support::ScanImageAssembler assembler;
  std::vector<unsigned char> buffer(256 * 1024);
  long long expected = 0;
  long long received = 0;
  const auto& sane = sane_library();
  try {
    while (!assembler.complete()) {
      int status = sane.start(handle);
      if (status == kSaneStatusCancelled || session.cancel_requested) {
        sane.cancel(handle);
        return {ScannerAcquireStatus::Cancelled, {}, {}};
      }
      if (status != kSaneStatusGood) {
        sane.cancel(handle);
        return failure(scan_failure(session, status));
      }
      SaneParameters parameters{};
      status = sane.get_parameters(handle, &parameters);
      if (status != kSaneStatusGood) {
        sane.cancel(handle);
        return failure(scan_failure(session, status));
      }
      assembler.begin_frame({static_cast<support::ScanFrameFormat>(parameters.format), parameters.last_frame != 0,
                             parameters.bytes_per_line, parameters.pixels_per_line, parameters.lines,
                             parameters.depth});
      const bool three_pass = parameters.format >= static_cast<int>(support::ScanFrameFormat::Red);
      if (parameters.lines > 0) {
        expected = static_cast<long long>(parameters.bytes_per_line) * parameters.lines * (three_pass ? 3 : 1);
      }
      for (;;) {
        int length = 0;
        status = sane.read(handle, buffer.data(), static_cast<int>(buffer.size()), &length);
        if (status == kSaneStatusEof) {
          break;
        }
        if (status == kSaneStatusCancelled || session.cancel_requested) {
          sane.cancel(handle);
          return {ScannerAcquireStatus::Cancelled, {}, {}};
        }
        if (status != kSaneStatusGood) {
          sane.cancel(handle);
          return failure(scan_failure(session, status));
        }
        if (length > 0) {
          assembler.append(std::span(buffer.data(), static_cast<std::size_t>(length)));
          received += length;
          if (expected > 0) {
            session.progress_percent = static_cast<int>(std::min<long long>(100, received * 100 / expected));
          }
        }
      }
      assembler.end_frame();
    }
  } catch (const std::exception& error) {
    sane.cancel(handle);
    return failure(QCoreApplication::translate("ScannerImport", "The scan could not be completed (%1)")
                       .arg(QString::fromUtf8(error.what())));
  }
  // A completed scan still needs sane_cancel to return the device to its idle state.
  sane.cancel(handle);

  const auto scan = assembler.finish();
  const auto image = image_from_scan(scan);
  if (image.isNull()) {
    return failure(failure_explanation(support::AcquireFailureCause::OutOfMemory, {}));
  }
  const auto path = temporary_scan_path(QStringLiteral("png"));
  if (path.isEmpty()) {
    return failure(QCoreApplication::translate("ScannerImport", "The temporary scan folder could not be created."));
  }
  QImageWriter writer(path, "png");
  writer.setCompression(1);
  if (!writer.write(image)) {
    QFile::remove(path);
    return failure(QCoreApplication::translate("ScannerImport", "The scan could not be completed (%1)")
                       .arg(writer.errorString()));
  }
  ScannerAcquireResult result;
  result.status = ScannerAcquireStatus::Acquired;
  result.file_path = path;
  const double dpi = read_resolution_dpi(handle);
  result.horizontal_dpi = dpi;
  result.vertical_dpi = dpi;
  return result;
}

QString safe_suffix(const QString& file_name) {
  auto suffix = QFileInfo(file_name).suffix().toLower();
  const bool clean = !suffix.isEmpty() && suffix.size() <= 8 &&
                     std::all_of(suffix.begin(), suffix.end(), [](QChar c) { return c.isLetterOrNumber() && c.unicode() < 128; });
  return clean ? suffix : QStringLiteral("img");
}

ScannerAcquireResult download_camera_image(Session& session, const CameraImage& image) {
  const auto& gp = gphoto_library();
  CameraFile* file = nullptr;
  int status = gp.file_new(&file);
  if (status >= kGpOk) {
    status = gp.file_get(session.camera, image.folder.toUtf8().constData(), image.name.toUtf8().constData(),
                         kGpFileTypeNormal, file, session.gphoto_context);
  }
  const char* data = nullptr;
  unsigned long size = 0;
  if (status >= kGpOk) {
    status = gp.file_get_data_and_size(file, &data, &size);
  }
  auto release = [&] {
    if (file != nullptr) {
      gp.file_unref(file);
    }
  };
  if (status < kGpOk || data == nullptr || size == 0) {
    release();
    return failure(describe_failure(
        QCoreApplication::translate("ScannerImport", "The camera image could not be downloaded (%1)")
            .arg(gphoto_status_text(status < kGpOk ? status : -1)),
        support::classify_gphoto_failure(status)));
  }
  const auto path = temporary_scan_path(safe_suffix(image.name));
  if (path.isEmpty()) {
    release();
    return failure(QCoreApplication::translate("ScannerImport", "The temporary scan folder could not be created."));
  }
  QFile output(path);
  const bool written = output.open(QIODevice::WriteOnly) &&
                       output.write(data, static_cast<qint64>(size)) == static_cast<qint64>(size);
  output.close();
  release();
  if (!written) {
    QFile::remove(path);
    return failure(QCoreApplication::translate("ScannerImport", "The camera image could not be downloaded (%1)")
                       .arg(output.errorString()));
  }
  ScannerAcquireResult result;
  result.status = ScannerAcquireStatus::Acquired;
  result.file_path = path;
  return result;
}

// ---------------------------------------------------------------------------------------

class AcquireDialog : public QDialog {
 public:
  explicit AcquireDialog(QWidget* parent) : QDialog(parent), session_(std::make_shared<Session>()) {
    setObjectName(QStringLiteral("linuxScannerDialog"));
    auto* root = new QVBoxLayout(this);
    auto* content = install_dark_dialog_chrome(
        *this, root, QCoreApplication::translate("ScannerImport", "Import from Scanner"));
    content->setSpacing(10);

    auto* form = new QFormLayout();
    devices_ = new QComboBox(this);
    devices_->setObjectName(QStringLiteral("linuxScannerDeviceCombo"));
    form->addRow(new QLabel(QCoreApplication::translate("ScannerImport", "Device:"), this), devices_);
    resolution_ = new QComboBox(this);
    resolution_->setObjectName(QStringLiteral("linuxScannerResolutionCombo"));
    resolution_label_ = new QLabel(QCoreApplication::translate("ScannerImport", "Resolution:"), this);
    form->addRow(resolution_label_, resolution_);
    mode_ = new QComboBox(this);
    mode_->setObjectName(QStringLiteral("linuxScannerModeCombo"));
    mode_label_ = new QLabel(QCoreApplication::translate("ScannerImport", "Mode:"), this);
    form->addRow(mode_label_, mode_);
    source_ = new QComboBox(this);
    source_->setObjectName(QStringLiteral("linuxScannerSourceCombo"));
    source_label_ = new QLabel(QCoreApplication::translate("ScannerImport", "Source:"), this);
    form->addRow(source_label_, source_);
    content->addLayout(form);

    images_ = new QListWidget(this);
    images_->setObjectName(QStringLiteral("linuxCameraImageList"));
    images_->setMinimumHeight(180);
    content->addWidget(images_);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("linuxScannerStatusLabel"));
    status_->setWordWrap(true);
    content->addWidget(status_);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("linuxScannerProgress"));
    progress_->setTextVisible(false);
    content->addWidget(progress_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("ScannerImport", "Cancel"));
    content->addWidget(buttons_);
    connect(buttons_, &QDialogButtonBox::accepted, this, [this] { start_acquire(); });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(devices_, &QComboBox::currentIndexChanged, this, [this](int) { open_selected_device(); });
    connect(images_, &QListWidget::itemDoubleClicked, this, [this] { start_acquire(); });
    connect(images_, &QListWidget::currentRowChanged, this, [this] { update_controls(); });

    progress_timer_ = new QTimer(this);
    progress_timer_->setInterval(100);
    connect(progress_timer_, &QTimer::timeout, this, [this] {
      const int percent = session_->progress_percent;
      if (percent >= 0) {
        progress_->setRange(0, 100);
        progress_->setValue(percent);
      }
    });

    resize(440, 220);
    show_scanner_controls(false);
    images_->setVisible(false);
    run_task(
        QCoreApplication::translate("ScannerImport", "Searching for scanners and cameras..."),
        [session = session_] { return enumerate_devices(*session); },
        [this](std::vector<DeviceEntry> devices) { devices_found(std::move(devices)); });
  }

  ~AcquireDialog() override {
    wait_for_worker();
    session_->close_all();
  }

  [[nodiscard]] ScannerAcquireResult result() const { return result_; }

  void reject() override {
    if (worker_ != nullptr) {
      // Let the running library call unwind first; the task's completion closes the dialog.
      close_after_task_ = true;
      session_->cancel_requested = true;
      if (auto* scanning = session_->active_scan.load(); scanning != nullptr) {
        sane_library().cancel(scanning);
      }
      buttons_->setEnabled(false);
      return;
    }
    QDialog::reject();
  }

 private:
  template <typename Work, typename Done>
  void run_task(const QString& message, Work work, Done done) {
    using Result = decltype(work());
    auto output = std::make_shared<std::optional<Result>>();
    status_->setText(message);
    progress_->setRange(0, 0);  // busy indicator until the worker reports progress
    progress_->setVisible(true);
    session_->progress_percent = -1;
    progress_timer_->start();
    worker_ = QThread::create([output, work = std::move(work)]() mutable { output->emplace(work()); });
    connect(worker_, &QThread::finished, this, [this, output, done = std::move(done)]() mutable {
      worker_->deleteLater();
      worker_ = nullptr;
      progress_timer_->stop();
      progress_->setVisible(false);
      status_->clear();
      if (close_after_task_) {
        if constexpr (std::is_same_v<Result, ScannerAcquireResult>) {
          if (output->value().status == ScannerAcquireStatus::Acquired) {
            QFile::remove(output->value().file_path);  // finished just as the user cancelled
          }
        }
        result_ = {ScannerAcquireStatus::Cancelled, {}, {}};
        QDialog::reject();
        return;
      }
      done(std::move(output->value()));
      update_controls();
    });
    update_controls();
    worker_->start();
  }

  void wait_for_worker() {
    if (worker_ != nullptr) {
      session_->cancel_requested = true;
      if (auto* scanning = session_->active_scan.load(); scanning != nullptr) {
        sane_library().cancel(scanning);
      }
      worker_->wait();
      delete worker_;
      worker_ = nullptr;
    }
  }

  void devices_found(std::vector<DeviceEntry> devices) {
    if (devices.empty()) {
      result_ = {ScannerAcquireStatus::NoDevice, {}, {}};
      QDialog::reject();
      return;
    }
    device_entries_ = std::move(devices);
    const QSignalBlocker blocker(devices_);
    for (const auto& device : device_entries_) {
      devices_->addItem(device.label);
    }
    devices_->setCurrentIndex(0);
    open_selected_device();
  }

  void open_selected_device() {
    const int index = devices_->currentIndex();
    if (index < 0 || static_cast<std::size_t>(index) >= device_entries_.size() || worker_ != nullptr) {
      return;
    }
    const auto device = device_entries_[static_cast<std::size_t>(index)];
    current_is_camera_ = device.camera;
    images_->clear();
    camera_images_.clear();
    run_task(
        device.camera ? QCoreApplication::translate("ScannerImport", "Reading the camera...")
                      : QCoreApplication::translate("ScannerImport", "Opening the scanner..."),
        [session = session_, device] { return open_device(*session, device); },
        [this](OpenResult opened) { device_opened(std::move(opened)); });
  }

  void device_opened(OpenResult opened) {
    device_ready_ = opened.error.isEmpty();
    if (!device_ready_) {
      status_->setText(opened.error);
      return;
    }
    if (current_is_camera_) {
      show_scanner_controls(false);
      images_->setVisible(true);
      camera_images_ = std::move(opened.camera_images);
      for (const auto& image : camera_images_) {
        auto* item = new QListWidgetItem(image.name, images_);
        item->setToolTip(image.folder);
      }
      if (camera_images_.empty()) {
        status_->setText(QCoreApplication::translate("ScannerImport", "No images were found on the camera."));
      } else {
        images_->setCurrentRow(images_->count() - 1);  // newest photos are listed last
      }
      return;
    }
    images_->setVisible(false);
    settings_ = std::move(opened.scanner);
    resolution_->clear();
    for (const int dpi : settings_.resolutions) {
      resolution_->addItem(QCoreApplication::translate("ScannerImport", "%1 dpi").arg(dpi), dpi);
    }
    resolution_->setCurrentIndex(std::max(0, resolution_->findData(settings_.resolution)));
    mode_->clear();
    mode_->addItems(settings_.modes);
    mode_->setCurrentIndex(std::max(0, static_cast<int>(settings_.modes.indexOf(settings_.mode))));
    source_->clear();
    source_->addItems(settings_.sources);
    source_->setCurrentIndex(std::max(0, static_cast<int>(settings_.sources.indexOf(settings_.source))));
    show_scanner_controls(true);
  }

  void show_scanner_controls(bool visible) {
    resolution_label_->setVisible(visible && !settings_.resolutions.empty());
    resolution_->setVisible(visible && !settings_.resolutions.empty());
    mode_label_->setVisible(visible && !settings_.modes.isEmpty());
    mode_->setVisible(visible && !settings_.modes.isEmpty());
    source_label_->setVisible(visible && settings_.sources.size() > 1);
    source_->setVisible(visible && settings_.sources.size() > 1);
  }

  void update_controls() {
    const bool idle = worker_ == nullptr;
    devices_->setEnabled(idle && devices_->count() > 1);
    resolution_->setEnabled(idle);
    mode_->setEnabled(idle);
    source_->setEnabled(idle);
    images_->setEnabled(idle);
    auto* ok = buttons_->button(QDialogButtonBox::Ok);
    ok->setText(current_is_camera_ ? QCoreApplication::translate("ScannerImport", "Import")
                                   : QCoreApplication::translate("ScannerImport", "Scan"));
    ok->setEnabled(idle && device_ready_ && (!current_is_camera_ || images_->currentRow() >= 0));
  }

  void start_acquire() {
    if (worker_ != nullptr || !device_ready_) {
      return;
    }
    if (current_is_camera_) {
      const int row = images_->currentRow();
      if (row < 0 || static_cast<std::size_t>(row) >= camera_images_.size()) {
        return;
      }
      const auto image = camera_images_[static_cast<std::size_t>(row)];
      run_task(
          QCoreApplication::translate("ScannerImport", "Downloading from the camera..."),
          [session = session_, image] { return download_camera_image(*session, image); },
          [this](ScannerAcquireResult acquired) { acquired_result(std::move(acquired)); });
      return;
    }
    auto settings = settings_;
    settings.resolution = resolution_->currentData().toInt();
    settings.mode = mode_->currentText();
    settings.source = source_->currentText();
    run_task(
        QCoreApplication::translate("ScannerImport", "Scanning..."),
        [session = session_, settings] { return run_scan(*session, settings); },
        [this](ScannerAcquireResult acquired) { acquired_result(std::move(acquired)); });
  }

  void acquired_result(ScannerAcquireResult acquired) {
    if (acquired.status == ScannerAcquireStatus::Failed) {
      status_->setText(acquired.error);  // stay open: the user can retry or pick another device
      return;
    }
    result_ = std::move(acquired);
    if (result_.status == ScannerAcquireStatus::Acquired) {
      QDialog::accept();
    } else {
      QDialog::reject();
    }
  }

  std::shared_ptr<Session> session_;
  QThread* worker_{nullptr};
  bool close_after_task_{false};
  bool device_ready_{false};
  bool current_is_camera_{false};
  std::vector<DeviceEntry> device_entries_;
  std::vector<CameraImage> camera_images_;
  ScannerSettings settings_;
  ScannerAcquireResult result_{ScannerAcquireStatus::Cancelled, {}, {}};
  QComboBox* devices_{nullptr};
  QLabel* resolution_label_{nullptr};
  QComboBox* resolution_{nullptr};
  QLabel* mode_label_{nullptr};
  QComboBox* mode_{nullptr};
  QLabel* source_label_{nullptr};
  QComboBox* source_{nullptr};
  QListWidget* images_{nullptr};
  QLabel* status_{nullptr};
  QProgressBar* progress_{nullptr};
  QTimer* progress_timer_{nullptr};
  QDialogButtonBox* buttons_{nullptr};
};

}  // namespace

ScannerAcquireResult acquire_image_from_scanner(QWidget* parent) {
  if (!sane_library().loaded && !gphoto_library().loaded) {
    return failure(QCoreApplication::translate(
        "ScannerImport", "Scanning needs SANE (for scanners) or libgphoto2 (for cameras). Install them "
                         "from your distribution and try again."));
  }
  AcquireDialog dialog(parent);
  exec_dialog(dialog);
  return dialog.result();
}

}  // namespace patchy::ui
