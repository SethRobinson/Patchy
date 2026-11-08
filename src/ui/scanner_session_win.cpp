// WIA 2.0 scanner session for Scan a Book to PDF (docs/book-scan.md): opens one device
// and scans its flatbed with fixed settings, no system dialog. Every WIA call runs on one
// worker thread in the multithreaded apartment that owns the device for the session's
// lifetime, so the window stays responsive and Cancel reaches the transfer callback.
// Compiled on Windows only (see CMakeLists).

#include "ui/scanner_session.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QPointer>

#include <windows.h>

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

QString wia_failure_text(HRESULT hr) {
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
  WiaScannerSession() = default;
  ~WiaScannerSession() override {
    cancel_ = true;
    worker_.call<int>([this]() {
      release_device();
      return 0;
    });
  }

  QString backend_name() const override {
    return QCoreApplication::translate("ScannerSession", "Windows Image Acquisition");
  }

  std::vector<ScannerDeviceInfo> devices() override {
    return worker_.call<std::vector<ScannerDeviceInfo>>([]() {
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
    });
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

  void cancel() override { cancel_ = true; }
  bool busy() const override { return busy_; }

private:
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
    if (FAILED(flatbed_->QueryInterface(IID_IWiaTransfer, reinterpret_cast<void**>(&transfer))) || transfer == nullptr) {
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
    result.error = wia_failure_text(FAILED(callback_error) ? callback_error : hr);
    return result;
  }

  ComWorker worker_;
  std::atomic<bool> cancel_{false};
  bool busy_{false};
  // Owned by the worker thread.
  IWiaItem2* root_{nullptr};
  IWiaItem2* flatbed_{nullptr};
  ScannerCapabilities capabilities_;
};

}  // namespace

std::unique_ptr<ScannerSession> create_wia_scanner_session() {
  return std::make_unique<WiaScannerSession>();
}

}  // namespace patchy::ui
