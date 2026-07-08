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
  if (item_root != nullptr) {
    item_root->Release();
  }

  if (hr == S_OK && file_count > 0 && file_paths != nullptr) {
    result.status = ScannerAcquireStatus::Acquired;
    result.file_path = QString::fromWCharArray(file_paths[0]);
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

  manager->Release();
  if (balance_uninitialize) {
    CoUninitialize();
  }
  return result;
}

}  // namespace patchy::ui
