// WIA 2.0 scanner/camera acquisition. IWiaDevMgr2::GetImageDlg shows the full system
// scan dialog (device selection included) and transfers straight to files in one call, so
// no IWiaTransfer callback machinery is needed. Compiled on Windows only (see CMakeLists).

#include "ui/scanner_import.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QWidget>

#include <windows.h>

#include <wia_lh.h>

namespace patchy::ui {

namespace {

QString hresult_text(HRESULT hr) {
  return QStringLiteral("0x%1").arg(static_cast<qulonglong>(static_cast<ULONG>(hr)), 8, 16, QLatin1Char('0'));
}

// Reads WIA_IPS_XRES/WIA_IPS_YRES from one WIA item. The scanned file's own density is
// unreliable (drivers write 72 or nothing into JPEGs), so the item properties, which
// hold the values the dialog just scanned with, are the trustworthy DPI source.
bool read_item_resolution(IUnknown* item, double* x_dpi, double* y_dpi) {
  IWiaPropertyStorage* storage = nullptr;
  if (FAILED(item->QueryInterface(IID_IWiaPropertyStorage, reinterpret_cast<void**>(&storage))) ||
      storage == nullptr) {
    return false;
  }
  PROPSPEC specs[2];
  specs[0].ulKind = PRSPEC_PROPID;
  specs[0].propid = WIA_IPS_XRES;
  specs[1].ulKind = PRSPEC_PROPID;
  specs[1].propid = WIA_IPS_YRES;
  PROPVARIANT values[2];
  PropVariantInit(&values[0]);
  PropVariantInit(&values[1]);
  const HRESULT hr = storage->ReadMultiple(2, specs, values);
  const bool ok = hr == S_OK && values[0].vt == VT_I4 && values[1].vt == VT_I4 && values[0].lVal > 0 &&
                  values[1].lVal > 0;
  if (ok) {
    *x_dpi = static_cast<double>(values[0].lVal);
    *y_dpi = static_cast<double>(values[1].lVal);
  }
  PropVariantClear(&values[0]);
  PropVariantClear(&values[1]);
  storage->Release();
  return ok;
}

// GetImageDlg returns the device root; the resolution lives on the transfer item (the
// flatbed or feeder child). The root is tried first, then the first child exposing the
// properties; with both a flatbed and a feeder this can only pick one, which is right
// whenever they share the dialog's resolution setting.
void read_scan_resolution(IWiaItem2* root, double* x_dpi, double* y_dpi) {
  if (root == nullptr || read_item_resolution(root, x_dpi, y_dpi)) {
    return;
  }
  IEnumWiaItem2* children = nullptr;
  if (FAILED(root->EnumChildItems(nullptr, &children)) || children == nullptr) {
    return;
  }
  IWiaItem2* child = nullptr;
  ULONG fetched = 0;
  while (children->Next(1, &child, &fetched) == S_OK && fetched == 1) {
    const bool ok = read_item_resolution(child, x_dpi, y_dpi);
    child->Release();
    if (ok) {
      break;
    }
  }
  children->Release();
}

}  // namespace

ScannerAcquireResult acquire_image_from_scanner(QWidget* parent) {
  ScannerAcquireResult result;

  // Qt already initializes COM on the GUI thread; S_FALSE / RPC_E_CHANGED_MODE simply mean
  // it is up and we must not tear it down.
  const HRESULT init_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool balance_uninitialize = SUCCEEDED(init_hr);

  IWiaDevMgr2* manager = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_IWiaDevMgr2,
                                reinterpret_cast<void**>(&manager));
  if (FAILED(hr) || manager == nullptr) {
    result.status = ScannerAcquireStatus::Failed;
    result.error = QCoreApplication::translate("ScannerImport", "Windows Image Acquisition is unavailable (%1)")
                       .arg(hresult_text(hr));
    if (balance_uninitialize) {
      CoUninitialize();
    }
    return result;
  }

  const auto folder = QDir::toNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
  const auto base_name = QStringLiteral("patchy-scan-%1").arg(QCoreApplication::applicationPid());
  BSTR folder_bstr = SysAllocString(reinterpret_cast<const wchar_t*>(folder.utf16()));
  BSTR file_bstr = SysAllocString(reinterpret_cast<const wchar_t*>(base_name.utf16()));
  LONG file_count = 0;
  BSTR* file_paths = nullptr;
  IWiaItem2* item_root = nullptr;
  const HWND owner = parent != nullptr ? reinterpret_cast<HWND>(parent->window()->winId()) : nullptr;

  hr = manager->GetImageDlg(0, nullptr, owner, folder_bstr, file_bstr, &file_count, &file_paths, &item_root);

  SysFreeString(folder_bstr);
  SysFreeString(file_bstr);

  if (hr == S_OK && file_count > 0 && file_paths != nullptr) {
    result.status = ScannerAcquireStatus::Acquired;
    result.file_path = QString::fromWCharArray(file_paths[0]);
    read_scan_resolution(item_root, &result.horizontal_dpi, &result.vertical_dpi);
  } else if (hr == S_FALSE) {
    result.status = ScannerAcquireStatus::Cancelled;
  } else if (hr == WIA_S_NO_DEVICE_AVAILABLE) {
    result.status = ScannerAcquireStatus::NoDevice;
  } else {
    result.status = ScannerAcquireStatus::Failed;
    result.error = QCoreApplication::translate("ScannerImport", "The scan could not be completed (%1)")
                       .arg(hresult_text(hr));
  }
  if (file_paths != nullptr) {
    // Extra pages beyond the first are deleted right away; Patchy imports one image.
    for (LONG i = 0; i < file_count; ++i) {
      if (i > 0) {
        DeleteFileW(file_paths[i]);
      }
      SysFreeString(file_paths[i]);
    }
    CoTaskMemFree(file_paths);
  }
  if (item_root != nullptr) {
    item_root->Release();
  }

  manager->Release();
  if (balance_uninitialize) {
    CoUninitialize();
  }
  return result;
}

}  // namespace patchy::ui
