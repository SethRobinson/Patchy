// WIA 2.0 scanner session for Scan a Book to PDF (docs/book-scan.md): opens one device
// and scans its flatbed with fixed settings, no system dialog. Every WIA call runs on one
// worker thread in the multithreaded apartment that owns the device for the session's
// lifetime, so the window stays responsive and Cancel reaches the transfer callback.
// While a session exists it listens for scanners turning on or off: WIA's device
// connected/disconnected events, plus WM_DEVICECHANGE as a second trigger. Either one
// starts a short debounce, then the list is read again on the worker thread
// (docs/book-scan-scanners.md, "Scanners turning on and off"). When asked to, it also listens for the
// opened scanner's own scan buttons (WIA's scan events, registered for that one device)
// and reports each press; it never starts a scan itself (docs/book-scan-scanners.md, "The scanner's
// own Scan button"). Compiled on Windows only (see CMakeLists).

#include "ui/scanner_session.hpp"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <windows.h>

#include <dbt.h>
#include <shlwapi.h>
#include <sti.h>
#include <wia_lh.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace patchy::ui {

namespace {

QString hresult_text(HRESULT hr) {
  return QStringLiteral("0x%1").arg(static_cast<qulonglong>(static_cast<ULONG>(hr)), 8, 16, QLatin1Char('0'));
}

// Errors that mean the device is gone or its handle is dead (switched off, unplugged,
// power-cycled, or the WIA service restarted): the session must open it again.
bool device_lost_error(HRESULT hr) {
  switch (hr) {
    case WIA_ERROR_OFFLINE:
    case WIA_ERROR_DEVICE_COMMUNICATION:
    case WIA_S_NO_DEVICE_AVAILABLE:
    case HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED):
    case HRESULT_FROM_WIN32(ERROR_NOT_READY):
    case HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE):
    case RPC_E_DISCONNECTED:
    case RPC_E_SERVER_DIED:
    case RPC_E_SERVER_DIED_DNE:
    case CO_E_OBJNOTCONNECTED:
      return true;
    default:
      return false;
  }
}

QString wia_failure_text(HRESULT hr) {
  if (device_lost_error(hr)) {
    return QCoreApplication::translate("ScannerSession",
                                       "The scanner stopped responding. Check that it is switched on and connected.");
  }
  switch (hr) {
    case WIA_ERROR_BUSY:
      return QCoreApplication::translate("ScannerSession", "The scanner is busy with another job or program.");
    case WIA_ERROR_OFFLINE:
    case WIA_ERROR_DEVICE_COMMUNICATION:
    case WIA_ERROR_DEVICE_LOCKED:
      return QCoreApplication::translate("ScannerSession",
                                         "The scanner stopped responding. Check that it is switched on and connected.");
    case WIA_ERROR_COVER_OPEN:
      return QCoreApplication::translate("ScannerSession", "The scanner reports that its cover is open.");
    case WIA_ERROR_PAPER_JAM:
      return QCoreApplication::translate("ScannerSession", "The scanner reports a paper jam.");
    case WIA_ERROR_LAMP_OFF:
      return QCoreApplication::translate("ScannerSession", "The scanner lamp is off.");
    case WIA_ERROR_WARMING_UP:
      return QCoreApplication::translate("ScannerSession", "The scanner is still warming up. Try again in a moment.");
    case E_OUTOFMEMORY:
      return QCoreApplication::translate("ScannerSession", "Not enough memory for this scan. Try a lower resolution.");
    default:
      break;
  }
  return QCoreApplication::translate("ScannerSession", "The scan could not be completed (%1).").arg(hresult_text(hr));
}

QString guid_text(const GUID& guid) {
  wchar_t buffer[64] = {};
  return StringFromGUID2(guid, buffer, 64) > 0 ? QString::fromWCharArray(buffer) : QStringLiteral("{?}");
}

struct NamedWiaEvent {
  const GUID* guid;
  const char* name;
};

// The events a scanner raises when the user presses one of its scan buttons ("WIA Event
// Identifiers" in the Windows documentation). A scanner with several buttons declares
// several of them; any one counts as a press of a scan button.
constexpr NamedWiaEvent kScanButtonEvents[] = {
    {&WIA_EVENT_SCAN_IMAGE, "WIA_EVENT_SCAN_IMAGE"},
    {&WIA_EVENT_SCAN_IMAGE2, "WIA_EVENT_SCAN_IMAGE2"},
    {&WIA_EVENT_SCAN_IMAGE3, "WIA_EVENT_SCAN_IMAGE3"},
    {&WIA_EVENT_SCAN_IMAGE4, "WIA_EVENT_SCAN_IMAGE4"},
    {&WIA_EVENT_SCAN_PRINT_IMAGE, "WIA_EVENT_SCAN_PRINT_IMAGE"},
    {&WIA_EVENT_SCAN_FAX_IMAGE, "WIA_EVENT_SCAN_FAX_IMAGE"},
    {&WIA_EVENT_SCAN_OCR_IMAGE, "WIA_EVENT_SCAN_OCR_IMAGE"},
    {&WIA_EVENT_SCAN_EMAIL_IMAGE, "WIA_EVENT_SCAN_EMAIL_IMAGE"},
    {&WIA_EVENT_SCAN_FILM_IMAGE, "WIA_EVENT_SCAN_FILM_IMAGE"},
};

// Other standard events, named only so the scanner log reads well. Lid, cover and feeder
// events are never listened for: a scan starts from the user's own press alone.
constexpr NamedWiaEvent kOtherWiaEvents[] = {
    {&WIA_EVENT_DEVICE_CONNECTED, "WIA_EVENT_DEVICE_CONNECTED"},
    {&WIA_EVENT_DEVICE_DISCONNECTED, "WIA_EVENT_DEVICE_DISCONNECTED"},
    {&WIA_EVENT_ITEM_CREATED, "WIA_EVENT_ITEM_CREATED"},
    {&WIA_EVENT_ITEM_DELETED, "WIA_EVENT_ITEM_DELETED"},
    {&WIA_EVENT_TREE_UPDATED, "WIA_EVENT_TREE_UPDATED"},
    {&WIA_EVENT_VOLUME_INSERT, "WIA_EVENT_VOLUME_INSERT"},
    {&WIA_EVENT_STORAGE_CREATED, "WIA_EVENT_STORAGE_CREATED"},
    {&WIA_EVENT_STORAGE_DELETED, "WIA_EVENT_STORAGE_DELETED"},
    {&WIA_EVENT_CANCEL_IO, "WIA_EVENT_CANCEL_IO"},
    {&WIA_EVENT_POWER_SUSPEND, "WIA_EVENT_POWER_SUSPEND"},
    {&WIA_EVENT_POWER_RESUME, "WIA_EVENT_POWER_RESUME"},
    {&WIA_EVENT_DEVICE_NOT_READY, "WIA_EVENT_DEVICE_NOT_READY"},
    {&WIA_EVENT_DEVICE_READY, "WIA_EVENT_DEVICE_READY"},
    {&WIA_EVENT_FLATBED_LID_OPEN, "WIA_EVENT_FLATBED_LID_OPEN"},
    {&WIA_EVENT_FLATBED_LID_CLOSED, "WIA_EVENT_FLATBED_LID_CLOSED"},
    {&WIA_EVENT_FEEDER_LOADED, "WIA_EVENT_FEEDER_LOADED"},
    {&WIA_EVENT_FEEDER_EMPTIED, "WIA_EVENT_FEEDER_EMPTIED"},
    {&WIA_EVENT_COVER_OPEN, "WIA_EVENT_COVER_OPEN"},
    {&WIA_EVENT_COVER_CLOSED, "WIA_EVENT_COVER_CLOSED"},
};

bool is_scan_button_event(const GUID& guid) {
  return std::any_of(std::begin(kScanButtonEvents), std::end(kScanButtonEvents),
                     [&guid](const NamedWiaEvent& event) { return *event.guid == guid; });
}

// "WIA_EVENT_SCAN_IMAGE", or "an event of the driver's own" for a GUID Windows does not define.
QString wia_event_name(const GUID& guid) {
  for (const NamedWiaEvent& event : kScanButtonEvents) {
    if (*event.guid == guid) {
      return QString::fromLatin1(event.name);
    }
  }
  for (const NamedWiaEvent& event : kOtherWiaEvents) {
    if (*event.guid == guid) {
      return QString::fromLatin1(event.name);
    }
  }
  return QStringLiteral("an event of the driver's own");
}

// Registers one event for one device. The Windows documentation warns that registering
// again in a process that registered before the Still Image service restarted "may cause
// an access violation"; that must cost the scanner's buttons and nothing else, so an
// access violation inside the call is turned into a failure. No C++ objects live in this
// function, as structured exception handling requires.
HRESULT register_wia_event(IWiaDevMgr2* manager, BSTR device_id, const GUID* event, IWiaEventCallback* sink,
                           IUnknown** registration) {
#if defined(_MSC_VER)
  __try {
    return manager->RegisterEventCallbackInterface(0, device_id, event, sink, registration);
  } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                              : EXCEPTION_CONTINUE_SEARCH) {
    return E_UNEXPECTED;
  }
#else
  return manager->RegisterEventCallbackInterface(0, device_id, event, sink, registration);
#endif
}

class PropertyStorage {
public:
  explicit PropertyStorage(IUnknown* item) {
    if (item != nullptr) {
      item->QueryInterface(IID_IWiaPropertyStorage, reinterpret_cast<void**>(&storage_));
    }
  }
  ~PropertyStorage() {
    if (storage_ != nullptr) {
      storage_->Release();
    }
  }
  PropertyStorage(const PropertyStorage&) = delete;
  PropertyStorage& operator=(const PropertyStorage&) = delete;

  [[nodiscard]] bool valid() const { return storage_ != nullptr; }

  bool read_long(PROPID id, LONG* value) const {
    if (storage_ == nullptr) {
      return false;
    }
    PROPSPEC spec{};
    spec.ulKind = PRSPEC_PROPID;
    spec.propid = id;
    PROPVARIANT var;
    PropVariantInit(&var);
    const bool ok = storage_->ReadMultiple(1, &spec, &var) == S_OK && var.vt == VT_I4;
    if (ok) {
      *value = var.lVal;
    }
    PropVariantClear(&var);
    return ok;
  }

  bool read_guid(PROPID id, GUID* value) const {
    if (storage_ == nullptr) {
      return false;
    }
    PROPSPEC spec{};
    spec.ulKind = PRSPEC_PROPID;
    spec.propid = id;
    PROPVARIANT var;
    PropVariantInit(&var);
    const bool ok = storage_->ReadMultiple(1, &spec, &var) == S_OK && var.vt == VT_CLSID && var.puuid != nullptr;
    if (ok) {
      *value = *var.puuid;
    }
    PropVariantClear(&var);
    return ok;
  }

  QString read_string(PROPID id) const {
    if (storage_ == nullptr) {
      return {};
    }
    PROPSPEC spec{};
    spec.ulKind = PRSPEC_PROPID;
    spec.propid = id;
    PROPVARIANT var;
    PropVariantInit(&var);
    QString text;
    if (storage_->ReadMultiple(1, &spec, &var) == S_OK && var.vt == VT_BSTR && var.bstrVal != nullptr) {
      text = QString::fromWCharArray(var.bstrVal);
    }
    PropVariantClear(&var);
    return text;
  }

  HRESULT write_long(PROPID id, LONG value) {
    if (storage_ == nullptr) {
      return E_POINTER;
    }
    PROPSPEC spec{};
    spec.ulKind = PRSPEC_PROPID;
    spec.propid = id;
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I4;
    var.lVal = value;
    return storage_->WriteMultiple(1, &spec, &var, WIA_IPA_FIRST);
  }

  HRESULT write_guid(PROPID id, const GUID& value) {
    if (storage_ == nullptr) {
      return E_POINTER;
    }
    PROPSPEC spec{};
    spec.ulKind = PRSPEC_PROPID;
    spec.propid = id;
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_CLSID;
    GUID copy = value;
    var.puuid = &copy;
    return storage_->WriteMultiple(1, &spec, &var, WIA_IPA_FIRST);
  }

  // The values a property accepts: the list, or standard resolutions inside the range.
  std::vector<int> allowed_resolutions(PROPID id) const {
    std::vector<int> values;
    if (storage_ == nullptr) {
      return values;
    }
    PROPSPEC spec{};
    spec.ulKind = PRSPEC_PROPID;
    spec.propid = id;
    ULONG flags = 0;
    PROPVARIANT var;
    PropVariantInit(&var);
    if (storage_->GetPropertyAttributes(1, &spec, &flags, &var) != S_OK) {
      PropVariantClear(&var);
      return values;
    }
    if ((flags & WIA_PROP_LIST) != 0 && var.vt == (VT_VECTOR | VT_I4) && var.cal.cElems > WIA_LIST_VALUES) {
      const ULONG count = std::min<ULONG>(static_cast<ULONG>(var.cal.pElems[WIA_LIST_COUNT]),
                                          var.cal.cElems - WIA_LIST_VALUES);
      for (ULONG i = 0; i < count; ++i) {
        values.push_back(var.cal.pElems[WIA_LIST_VALUES + i]);
      }
    } else if ((flags & WIA_PROP_RANGE) != 0 && var.vt == (VT_VECTOR | VT_I4) && var.cal.cElems > WIA_RANGE_STEP) {
      const LONG minimum = var.cal.pElems[WIA_RANGE_MIN];
      const LONG maximum = var.cal.pElems[WIA_RANGE_MAX];
      const LONG step = std::max<LONG>(1, var.cal.pElems[WIA_RANGE_STEP]);
      for (const int standard : {75, 100, 150, 200, 240, 300, 400, 600, 1200}) {
        if (standard >= minimum && standard <= maximum && (standard - minimum) % step == 0) {
          values.push_back(standard);
        }
      }
    }
    PropVariantClear(&var);
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
  }

private:
  IWiaPropertyStorage* storage_{nullptr};
};

// Hands WIA a file stream and relays progress; S_FALSE from TransferCallback cancels.
class WiaTransferSink final : public IWiaTransferCallback {
public:
  WiaTransferSink(std::wstring path, std::atomic<bool>* cancel, std::function<void(int)> progress)
      : path_(std::move(path)), cancel_(cancel), progress_(std::move(progress)) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IWiaTransferCallback) {
      *object = static_cast<IWiaTransferCallback*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --refs_;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE TransferCallback(LONG, WiaTransferParams* params) override {
    if (params != nullptr) {
      if (params->lMessage == WIA_TRANSFER_MSG_STATUS && progress_) {
        progress_(static_cast<int>(params->lPercentComplete));
      }
      if (FAILED(params->hrErrorStatus)) {
        error_ = params->hrErrorStatus;
      }
    }
    return cancel_->load() ? S_FALSE : S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetNextStream(LONG, BSTR, BSTR, IStream** destination) override {
    if (destination == nullptr) {
      return E_POINTER;
    }
    if (streams_ > 0) {
      return S_FALSE;  // one image per scan
    }
    ++streams_;
    return SHCreateStreamOnFileEx(path_.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, FILE_ATTRIBUTE_NORMAL,
                                  TRUE, nullptr, destination);
  }

  [[nodiscard]] HRESULT error() const { return error_; }

private:
  ~WiaTransferSink() = default;
  std::atomic<ULONG> refs_{1};
  std::wstring path_;
  std::atomic<bool>* cancel_;
  std::function<void(int)> progress_;
  HRESULT error_{S_OK};
  int streams_{0};
};

// Relays WIA device events with the event's identifier and the device it came from.
// Called on an RPC thread; `notify` must be thread-safe.
class WiaEventSink final : public IWiaEventCallback {
public:
  explicit WiaEventSink(std::function<void(const GUID&, const QString&)> notify) : notify_(std::move(notify)) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IWiaEventCallback) {
      *object = static_cast<IWiaEventCallback*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --refs_;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE ImageEventCallback(const GUID* event, BSTR, BSTR device_id, BSTR, DWORD, BSTR, ULONG*,
                                               ULONG) override {
    notify_(event != nullptr ? *event : GUID{}, device_id != nullptr ? QString::fromWCharArray(device_id) : QString());
    return S_OK;
  }

private:
  ~WiaEventSink() = default;
  std::atomic<ULONG> refs_{1};
  std::function<void(const GUID&, const QString&)> notify_;
};

// The scanners WIA lists as present. WIA_DEVINFO_ENUM_LOCAL leaves out installed
// scanners that are switched off or unplugged (WIA_DEVINFO_ENUM_ALL would list them).
// Runs on the worker thread.
std::vector<ScannerDeviceInfo> enumerate_wia_scanners() {
  std::vector<ScannerDeviceInfo> found;
  IWiaDevMgr2* manager = nullptr;
  if (FAILED(CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_IWiaDevMgr2,
                              reinterpret_cast<void**>(&manager))) ||
      manager == nullptr) {
    return found;
  }
  IEnumWIA_DEV_INFO* devices = nullptr;
  if (SUCCEEDED(manager->EnumDeviceInfo(WIA_DEVINFO_ENUM_LOCAL, &devices)) && devices != nullptr) {
    IWiaPropertyStorage* info = nullptr;
    ULONG fetched = 0;
    while (devices->Next(1, &info, &fetched) == S_OK && fetched == 1) {
      PropertyStorage storage(info);
      LONG type = 0;
      storage.read_long(WIA_DIP_DEV_TYPE, &type);
      if (GET_STIDEVICE_TYPE(type) == StiDeviceTypeScanner) {
        found.push_back({storage.read_string(WIA_DIP_DEV_ID), storage.read_string(WIA_DIP_DEV_NAME)});
      }
      info->Release();
    }
    devices->Release();
  }
  manager->Release();
  return found;
}

bool same_devices(const std::vector<ScannerDeviceInfo>& a, const std::vector<ScannerDeviceInfo>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const ScannerDeviceInfo& x, const ScannerDeviceInfo& y) {
    return x.id == y.id && x.name == y.name;
  });
}

// WM_DEVICECHANGE (DBT_DEVNODES_CHANGED) reaches every top-level window when any device
// node changes; installed only while a WIA session exists.
class DeviceChangeFilter final : public QAbstractNativeEventFilter {
public:
  explicit DeviceChangeFilter(std::function<void()> changed) : changed_(std::move(changed)) {}
  bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override {
    if (type == "windows_generic_MSG" && message != nullptr) {
      const auto* msg = static_cast<const MSG*>(message);
      if (msg->message == WM_DEVICECHANGE && msg->wParam == DBT_DEVNODES_CHANGED) {
        changed_();
      }
    }
    return false;
  }

private:
  std::function<void()> changed_;
};

// One thread in the multithreaded apartment that runs every WIA call in order.
class ComWorker {
public:
  ComWorker() : thread_([this]() { run(); }) {}
  ~ComWorker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_all();
    thread_.join();
  }
  ComWorker(const ComWorker&) = delete;
  ComWorker& operator=(const ComWorker&) = delete;

  void post(std::function<void()> job) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      jobs_.push_back(std::move(job));
    }
    wake_.notify_all();
  }

  template <typename Result>
  Result call(std::function<Result()> job) {
    auto task = std::make_shared<std::packaged_task<Result()>>(std::move(job));
    std::future<Result> future = task->get_future();
    post([task]() { (*task)(); });
    return future.get();
  }

private:
  void run() {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this]() { return stopping_ || !jobs_.empty(); });
        if (jobs_.empty()) {
          break;
        }
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }
      job();
    }
    if (SUCCEEDED(init)) {
      CoUninitialize();
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> jobs_;
  bool stopping_{false};
  std::thread thread_;
};

class WiaScannerSession final : public ScannerSession {
public:
  WiaScannerSession() {
    rescan_timer_.setSingleShot(true);
    connect(&rescan_timer_, &QTimer::timeout, this, &WiaScannerSession::rescan);
    QPointer<WiaScannerSession> self(this);
    start_scanner_log_file();
    // WIA calls the sink on an RPC thread: hop to the GUI thread before touching the timer.
    auto on_wia_event = [self](const GUID&, const QString&) {
      QMetaObject::invokeMethod(
          qApp,
          [self]() {
            if (self) {
              self->wia_event_seen_ = true;
              self->schedule_rescan(kWiaEventDebounceMs);
            }
          },
          Qt::QueuedConnection);
    };
    worker_.post([this, on_wia_event]() { register_events_on_worker(on_wia_event); });
    if (QCoreApplication::instance() != nullptr) {
      device_filter_ = std::make_unique<DeviceChangeFilter>([this]() { schedule_rescan(kDeviceNodeDebounceMs); });
      QCoreApplication::instance()->installNativeEventFilter(device_filter_.get());
    }
  }
  ~WiaScannerSession() override {
    if (device_filter_ != nullptr && QCoreApplication::instance() != nullptr) {
      QCoreApplication::instance()->removeNativeEventFilter(device_filter_.get());
    }
    cancel_ = true;
    worker_.call<int>([this]() {
      unregister_buttons_on_worker();
      release_device();
      unregister_events_on_worker();
      return 0;
    });
  }

  QString backend_name() const override {
    return QCoreApplication::translate("ScannerSession", "Windows Image Acquisition");
  }

  std::vector<ScannerDeviceInfo> devices() override {
    last_devices_ = worker_.call<std::vector<ScannerDeviceInfo>>([]() { return enumerate_wia_scanners(); });
    return last_devices_;
  }

  bool open(const QString& device_id, QString* error) override {
    const QString message = worker_.call<QString>([this, device_id]() { return open_on_worker(device_id); });
    if (error != nullptr) {
      *error = message;
    }
    return message.isEmpty();
  }

  ScannerCapabilities capabilities() const override { return capabilities_; }

  void start_scan(const ScanRequest& request) override {
    if (busy_) {
      return;
    }
    busy_ = true;
    cancel_ = false;
    QPointer<WiaScannerSession> self(this);
    worker_.post([this, self, request]() {
      ScanResult result = scan_on_worker(request, self);
      QMetaObject::invokeMethod(
          qApp,
          [self, result]() {
            if (self) {
              self->busy_ = false;
              emit self->finished(result);
            }
          },
          Qt::QueuedConnection);
    });
  }

  void close() override {
    worker_.post([this]() { release_device(); });
  }

  void cancel() override { cancel_ = true; }
  bool busy() const override { return busy_; }

  bool has_scan_button() const override { return has_scan_button_; }

  void set_scan_button_listening(bool listen) override {
    if (listen == listening_) {
      return;
    }
    listening_ = listen;
    worker_.post([this, listen]() {
      listen_wanted_ = listen;
      unregister_buttons_on_worker();
      if (listen) {
        register_buttons_on_worker();
      }
    });
  }

private:
  // WIA reports a device a little after it appears; a power switch can bounce.
  static constexpr int kWiaEventDebounceMs = 400;
  // Device nodes change before WIA has caught up, and for every other device too.
  static constexpr int kDeviceNodeDebounceMs = 1500;

  void schedule_rescan(int delay_ms) {
    // A pending shorter wait is kept; a longer one is cut to this one.
    if (!rescan_timer_.isActive() || rescan_timer_.remainingTime() > delay_ms) {
      rescan_timer_.start(delay_ms);
    }
  }

  // Reads the list on the worker thread, then tells the dialog if anything changed. A WIA
  // event always counts: a scanner switched off and on within the debounce lists the same,
  // but its old handle is dead.
  void rescan() {
    const bool event_seen = wia_event_seen_;
    wia_event_seen_ = false;
    QPointer<WiaScannerSession> self(this);
    worker_.post([self, event_seen]() {
      std::vector<ScannerDeviceInfo> found = enumerate_wia_scanners();
      QMetaObject::invokeMethod(
          qApp,
          [self, event_seen, found = std::move(found)]() {
            if (!self) {
              return;
            }
            if (!event_seen && same_devices(found, self->last_devices_)) {
              return;
            }
            self->last_devices_ = found;
            emit self->devices_changed(found);
          },
          Qt::QueuedConnection);
    });
  }

  // GUI thread: a scan button event arrived. Reported only while the dialog listens; one
  // queued before listening stopped is dropped here.
  void button_event(const GUID& event, const QString& device_id) {
    const QString what = QStringLiteral("button event %1 %2 from %3").arg(guid_text(event), wia_event_name(event), device_id);
    if (!listening_) {
      scanner_log(what + QStringLiteral(": ignored, not listening"));
      return;
    }
    scanner_log(what);
    emit scan_button_pressed(device_id);
  }

  // Which events the opened scanner declares; the scan button events among them are the
  // ones to listen for. A driver that does not answer declares none. Worker thread.
  void read_declared_events_on_worker(const QString& device_id) {
    button_events_.clear();
    button_device_id_ = device_id;
    IEnumWIA_DEV_CAPS* declared = nullptr;
    const HRESULT hr = root_->EnumDeviceCapabilities(WIA_DEVICE_EVENTS, &declared);
    if (FAILED(hr) || declared == nullptr) {
      scanner_log(QStringLiteral("scanner %1: its events could not be read (%2)").arg(device_id, hresult_text(hr)));
      return;
    }
    scanner_log(QStringLiteral("scanner %1 declares these events:").arg(device_id));
    int count = 0;
    WIA_DEV_CAP event{};
    ULONG fetched = 0;
    // A driver's list is a handful of entries; the limit only guards against one that never ends.
    while (count < 256 && declared->Next(1, &event, &fetched) == S_OK && fetched == 1) {
      ++count;
      const bool button = is_scan_button_event(event.guid);
      QStringList kinds;
      if ((event.ulFlags & WIA_ACTION_EVENT) != 0) {
        kinds << QStringLiteral("action");
      }
      if ((event.ulFlags & WIA_NOTIFICATION_EVENT) != 0) {
        kinds << QStringLiteral("notification");
      }
      scanner_log(QStringLiteral("  %1 %2 \"%3\" (%4)%5")
                      .arg(guid_text(event.guid), wia_event_name(event.guid),
                           event.bstrName != nullptr ? QString::fromWCharArray(event.bstrName) : QString(),
                           kinds.isEmpty() ? QStringLiteral("no kind") : kinds.join(QStringLiteral(", ")),
                           button ? QStringLiteral(": a scan button") : QString()));
      if (button && std::find(button_events_.begin(), button_events_.end(), event.guid) == button_events_.end()) {
        button_events_.push_back(event.guid);
      }
      for (BSTR text : {event.bstrName, event.bstrDescription, event.bstrIcon, event.bstrCommandline}) {
        SysFreeString(text);
      }
      event = {};
    }
    declared->Release();
    scanner_log(QStringLiteral("scanner %1: %2 event(s) declared, %3 of them scan buttons")
                    .arg(device_id)
                    .arg(count)
                    .arg(button_events_.size()));
  }

  // Registers the opened scanner's scan button events, for that one device. Every failure
  // is quiet: the scanner's buttons then simply do nothing here. Worker thread.
  void register_buttons_on_worker() {
    if (button_events_.empty() || button_device_id_.isEmpty()) {
      return;
    }
    IWiaDevMgr2* manager = nullptr;
    const HRESULT created = CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_IWiaDevMgr2,
                                             reinterpret_cast<void**>(&manager));
    if (FAILED(created) || manager == nullptr) {
      scanner_log(QStringLiteral("listening for scan buttons failed: no device manager (%1)").arg(hresult_text(created)));
      return;
    }
    QPointer<WiaScannerSession> self(this);
    // WIA calls the sink on an RPC thread: hop to the GUI thread.
    auto* sink = new WiaEventSink([self](const GUID& event, const QString& device_id) {
      QMetaObject::invokeMethod(
          qApp,
          [self, event, device_id]() {
            if (self) {
              self->button_event(event, device_id);
            }
          },
          Qt::QueuedConnection);
    });
    BSTR id = SysAllocString(reinterpret_cast<const wchar_t*>(button_device_id_.utf16()));
    for (const GUID& event : button_events_) {
      IUnknown* registration = nullptr;
      const HRESULT hr = register_wia_event(manager, id, &event, sink, &registration);
      if (SUCCEEDED(hr) && registration != nullptr) {
        button_registrations_.push_back(registration);
        scanner_log(QStringLiteral("listening for %1 %2 on %3").arg(guid_text(event), wia_event_name(event), button_device_id_));
      } else {
        scanner_log(QStringLiteral("listening for %1 %2 was refused (%3)")
                        .arg(guid_text(event), wia_event_name(event), hresult_text(hr)));
      }
    }
    SysFreeString(id);
    sink->Release();
    manager->Release();
  }

  void unregister_buttons_on_worker() {
    // Releasing the event objects unregisters the callbacks: the buttons are Windows' again.
    if (!button_registrations_.empty()) {
      scanner_log(QStringLiteral("stopped listening for scan buttons"));
    }
    for (IUnknown* registration : button_registrations_) {
      registration->Release();
    }
    button_registrations_.clear();
  }

  void register_events_on_worker(const std::function<void(const GUID&, const QString&)>& notify) {
    IWiaDevMgr2* manager = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_IWiaDevMgr2,
                                reinterpret_cast<void**>(&manager))) ||
        manager == nullptr) {
      return;
    }
    auto* sink = new WiaEventSink(notify);
    // Fails quietly: WM_DEVICECHANGE and the Refresh button still update the list.
    manager->RegisterEventCallbackInterface(0, nullptr, &WIA_EVENT_DEVICE_CONNECTED, sink, &connected_event_);
    manager->RegisterEventCallbackInterface(0, nullptr, &WIA_EVENT_DEVICE_DISCONNECTED, sink, &disconnected_event_);
    sink->Release();
    manager->Release();
  }

  void unregister_events_on_worker() {
    // Releasing the event objects unregisters the callbacks.
    for (IUnknown** event : {&connected_event_, &disconnected_event_}) {
      if (*event != nullptr) {
        (*event)->Release();
        *event = nullptr;
      }
    }
  }

  void release_device() {
    if (flatbed_ != nullptr) {
      flatbed_->Release();
      flatbed_ = nullptr;
    }
    if (root_ != nullptr) {
      root_->Release();
      root_ = nullptr;
    }
  }

  QString open_on_worker(const QString& device_id) {
    release_device();
    capabilities_ = {};
    // What the last scanner declared no longer counts: this open answers it afresh.
    unregister_buttons_on_worker();
    button_events_.clear();
    button_device_id_.clear();
    has_scan_button_ = false;
    IWiaDevMgr2* manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_IWiaDevMgr2,
                                  reinterpret_cast<void**>(&manager));
    if (FAILED(hr) || manager == nullptr) {
      return QCoreApplication::translate("ScannerSession", "Windows Image Acquisition is unavailable (%1)")
          .arg(hresult_text(hr));
    }
    BSTR id = SysAllocString(reinterpret_cast<const wchar_t*>(device_id.utf16()));
    hr = manager->CreateDevice(0, id, &root_);
    SysFreeString(id);
    manager->Release();
    if (FAILED(hr) || root_ == nullptr) {
      root_ = nullptr;
      return wia_failure_text(hr);
    }
    // The flatbed child: WIA_CATEGORY_FLATBED, else the first child with a resolution.
    IEnumWiaItem2* children = nullptr;
    IWiaItem2* fallback = nullptr;
    if (SUCCEEDED(root_->EnumChildItems(nullptr, &children)) && children != nullptr) {
      IWiaItem2* child = nullptr;
      ULONG fetched = 0;
      while (flatbed_ == nullptr && children->Next(1, &child, &fetched) == S_OK && fetched == 1) {
        PropertyStorage storage(child);
        GUID category{};
        LONG resolution = 0;
        if (storage.read_guid(WIA_IPA_ITEM_CATEGORY, &category) && category == WIA_CATEGORY_FLATBED) {
          flatbed_ = child;
          continue;
        }
        if (fallback == nullptr && storage.read_long(WIA_IPS_XRES, &resolution)) {
          fallback = child;
          continue;
        }
        child->Release();
      }
      children->Release();
    }
    if (flatbed_ == nullptr) {
      flatbed_ = fallback;
      fallback = nullptr;
    }
    if (fallback != nullptr) {
      fallback->Release();
    }
    if (flatbed_ == nullptr) {
      release_device();
      return QCoreApplication::translate("ScannerSession",
                                         "This scanner has no flatbed that Patchy can control directly.");
    }
    PropertyStorage item(flatbed_);
    PropertyStorage root(root_);
    capabilities_.direct_control = true;
    capabilities_.scan_area = true;
    capabilities_.resolutions = item.allowed_resolutions(WIA_IPS_XRES);
    LONG width = 0;
    LONG height = 0;
    if (!(item.read_long(WIA_IPS_MAX_HORIZONTAL_SIZE, &width) && item.read_long(WIA_IPS_MAX_VERTICAL_SIZE, &height))) {
      root.read_long(WIA_DPS_HORIZONTAL_BED_SIZE, &width);
      root.read_long(WIA_DPS_VERTICAL_BED_SIZE, &height);
    }
    capabilities_.bed_width_inches = width > 0 ? width / 1000.0 : 0.0;
    capabilities_.bed_height_inches = height > 0 ? height / 1000.0 : 0.0;
    // The scanner's own buttons: read after everything a scan needs, and never a reason
    // for open() to fail.
    read_declared_events_on_worker(device_id);
    has_scan_button_ = !button_events_.empty();
    if (listen_wanted_) {
      register_buttons_on_worker();
    }
    return {};
  }

  ScanResult scan_on_worker(const ScanRequest& request, QPointer<WiaScannerSession> self) {
    ScanResult result;
    if (flatbed_ == nullptr) {
      result.error = QCoreApplication::translate("ScannerSession", "No scanner is open.");
      return result;
    }
    PropertyStorage item(flatbed_);
    const bool gray = request.mode == ScanColorMode::Grayscale;
    // Data type and depth first, then resolution, then the area in pixels at that
    // resolution: drivers validate the extents against the current resolution.
    item.write_long(WIA_IPA_DATATYPE, gray ? WIA_DATA_GRAYSCALE : WIA_DATA_COLOR);
    item.write_long(WIA_IPA_DEPTH, gray ? 8 : 24);
    HRESULT hr = item.write_long(WIA_IPS_XRES, request.dpi);
    if (SUCCEEDED(hr)) {
      hr = item.write_long(WIA_IPS_YRES, request.dpi);
    }
    if (device_lost_error(hr)) {
      result.device_lost = true;
      result.error = wia_failure_text(hr);
      return result;
    }
    if (FAILED(hr)) {
      result.error = QCoreApplication::translate("ScannerSession", "The scanner does not accept %1 dpi.").arg(request.dpi);
      return result;
    }
    const double bed_width = capabilities_.bed_width_inches;
    const double bed_height = capabilities_.bed_height_inches;
    QRectF area = request.area_inches.value_or(QRectF(0.0, 0.0, bed_width, bed_height));
    if (bed_width > 0.0 && bed_height > 0.0) {
      area = area.intersected(QRectF(0.0, 0.0, bed_width, bed_height));
    }
    bool area_applied = false;
    if (area.width() > 0.0 && area.height() > 0.0) {
      // Origin first so the new extents always fit, then the extents, then the origin.
      item.write_long(WIA_IPS_XPOS, 0);
      item.write_long(WIA_IPS_YPOS, 0);
      const auto x = static_cast<LONG>(area.x() * request.dpi);
      const auto y = static_cast<LONG>(area.y() * request.dpi);
      const auto w = static_cast<LONG>(area.width() * request.dpi);
      const auto h = static_cast<LONG>(area.height() * request.dpi);
      area_applied = SUCCEEDED(item.write_long(WIA_IPS_XEXTENT, w)) && SUCCEEDED(item.write_long(WIA_IPS_YEXTENT, h)) &&
                     SUCCEEDED(item.write_long(WIA_IPS_XPOS, x)) && SUCCEEDED(item.write_long(WIA_IPS_YPOS, y));
    }
    item.write_long(WIA_IPA_TYMED, TYMED_FILE);
    item.write_guid(WIA_IPA_FORMAT, WiaImgFmt_BMP);

    IWiaTransfer* transfer = nullptr;
    hr = flatbed_->QueryInterface(IID_IWiaTransfer, reinterpret_cast<void**>(&transfer));
    if (FAILED(hr) || transfer == nullptr) {
      result.device_lost = device_lost_error(hr);
      result.error = QCoreApplication::translate("ScannerSession", "The scanner does not support direct transfers.");
      return result;
    }
    const QString path = QDir::toNativeSeparators(request.output_base_path + QStringLiteral(".bmp"));
    auto* callback = new WiaTransferSink(path.toStdWString(), &cancel_, [self](int percent) {
      QMetaObject::invokeMethod(
          qApp,
          [self, percent]() {
            if (self) {
              emit self->progress(percent);
            }
          },
          Qt::QueuedConnection);
    });
    // A scanner warming up refuses the first transfers; keep trying for a minute.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (;;) {
      hr = transfer->Download(0, callback);
      if (hr != WIA_ERROR_WARMING_UP || cancel_ || std::chrono::steady_clock::now() > deadline) {
        break;
      }
      QMetaObject::invokeMethod(
          qApp,
          [self]() {
            if (self) {
              emit self->status_message(QCoreApplication::translate("ScannerSession", "Scanner warming up..."));
            }
          },
          Qt::QueuedConnection);
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    const HRESULT callback_error = callback->error();
    callback->Release();
    transfer->Release();
    if (hr == S_OK && SUCCEEDED(callback_error)) {
      result.status = ScannerAcquireStatus::Acquired;
      result.file_path = QDir::fromNativeSeparators(path);
      result.horizontal_dpi = request.dpi;
      result.vertical_dpi = request.dpi;
      result.area_applied = area_applied;
      LONG xres = 0;
      LONG yres = 0;
      if (item.read_long(WIA_IPS_XRES, &xres) && item.read_long(WIA_IPS_YRES, &yres) && xres > 0 && yres > 0) {
        result.horizontal_dpi = xres;
        result.vertical_dpi = yres;
      }
      return result;
    }
    DeleteFileW(reinterpret_cast<const wchar_t*>(path.utf16()));
    if (hr == S_FALSE || cancel_) {
      result.status = ScannerAcquireStatus::Cancelled;
      return result;
    }
    result.status = ScannerAcquireStatus::Failed;
    const HRESULT failure = FAILED(callback_error) ? callback_error : hr;
    result.device_lost = device_lost_error(failure);
    result.error = wia_failure_text(failure);
    return result;
  }

  ComWorker worker_;
  std::atomic<bool> cancel_{false};
  bool busy_{false};
  // GUI thread.
  QTimer rescan_timer_;
  bool wia_event_seen_{false};
  std::vector<ScannerDeviceInfo> last_devices_;
  std::unique_ptr<DeviceChangeFilter> device_filter_;
  // Owned by the worker thread.
  IWiaItem2* root_{nullptr};
  IWiaItem2* flatbed_{nullptr};
  IUnknown* connected_event_{nullptr};
  IUnknown* disconnected_event_{nullptr};
  ScannerCapabilities capabilities_;
  // The scan button events the scanner opened last declares, and the registrations for
  // them while listening. Owned by the worker thread.
  std::vector<GUID> button_events_;
  QString button_device_id_;
  std::vector<IUnknown*> button_registrations_;
  bool listen_wanted_{false};
  // Written inside open() (the GUI thread waits for it there), read on the GUI thread.
  bool has_scan_button_{false};
  // GUI thread: what set_scan_button_listening() asked for last.
  bool listening_{false};
};

}  // namespace

std::unique_ptr<ScannerSession> create_wia_scanner_session() {
  return std::make_unique<WiaScannerSession>();
}

}  // namespace patchy::ui
