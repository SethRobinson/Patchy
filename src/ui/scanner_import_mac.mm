// macOS scanner acquisition through ImageKit. IKDeviceBrowserView supplies the native
// local/network device list and IKScannerDeviceView owns scanner sessions, scan settings,
// overview/crop UI, and file transfer. Compiled with ARC on macOS only (see CMakeLists).

#include "ui/scanner_import.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QUuid>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <utility>

#import <AppKit/AppKit.h>
#import <ImageCaptureCore/ImageCaptureCore.h>
#import <Quartz/Quartz.h>
#import <dispatch/dispatch.h>

namespace {

bool platform_is_cocoa() {
  return QGuiApplication::platformName().compare(QStringLiteral("cocoa"), Qt::CaseInsensitive) == 0;
}

NSWindow* ns_window_for_widget(QWidget* widget) {
  if (widget == nullptr) {
    return nil;
  }
  QWindow* handle = widget->window()->windowHandle();
  if (handle == nullptr || handle->handle() == nullptr) {
    return nil;
  }
  auto* view = (__bridge NSView*)reinterpret_cast<void*>(handle->winId());
  return view != nil ? view.window : nil;
}

NSString* ns_string(const QString& value) {
  const auto utf8 = value.toUtf8();
  return [NSString stringWithUTF8String:utf8.constData()];
}

QString error_description(NSError* error) {
  if (error == nil || error.localizedDescription == nil) {
    return QCoreApplication::translate("ScannerImport", "Unknown scanner error");
  }
  return QString::fromUtf8(error.localizedDescription.UTF8String);
}

bool error_is_scan_cancelled(NSError* error) {
  if (error == nil || ![error.domain isEqualToString:ICErrorDomain]) {
    return false;
  }
  return error.code == ICReturnScanOperationCanceled || error.code == ICReturnDownloadCanceled;
}

QString path_from_url(NSURL* url) {
  if (url == nil || !url.fileURL || url.fileSystemRepresentation == nullptr) {
    return {};
  }
  return QString::fromUtf8(url.fileSystemRepresentation);
}

void remove_scan_files_except(const QString& folderPath, const QString& documentName, const QString& keepPath) {
  const auto normalizedPath = [](const QString& path) {
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
  };
  const auto kept = normalizedPath(keepPath);
  QDir folder(folderPath);
  const auto entries = folder.entryInfoList(QStringList{documentName + QLatin1Char('*')},
                                            QDir::Files | QDir::NoSymLinks);
  for (const auto& entry : entries) {
    if (keepPath.isEmpty() || normalizedPath(entry.absoluteFilePath()) != kept) {
      QFile::remove(entry.absoluteFilePath());
    }
  }
}

struct ScannerAsyncState {
  patchy::ui::ScannerAcquireResult result;
  patchy::ui::ScannerAcquireCallback completion;
  QString downloads_folder;
  QString document_name;
};

}  // namespace

@interface PatchyScannerSheetController
    : NSObject <IKDeviceBrowserViewDelegate, IKScannerDeviceViewDelegate, NSWindowDelegate, NSSplitViewDelegate> {
 @private
  NSWindow* _sheet;
  NSWindow* _parentWindow;
  IKDeviceBrowserView* _browserView;
  IKScannerDeviceView* _scannerView;
  __strong ICScannerDevice* _selectedScanner;
  ScannerAsyncState* _state;
  BOOL _finished;
  BOOL _completionDelivered;
}

- (instancetype)initWithParentWindow:(NSWindow*)parentWindow
                                state:(ScannerAsyncState*)state;
- (void)beginSheet;
- (void)cancelCurrentScanner;
- (void)handleSheetEnded;
- (void)handleCompletedScanToURL:(NSURL*)url error:(NSError*)error;
- (void)tearDownAndEndSheet;

@end

@implementation PatchyScannerSheetController

- (instancetype)initWithParentWindow:(NSWindow*)parentWindow
                                state:(ScannerAsyncState*)state {
  self = [super init];
  if (self == nil) {
    return nil;
  }

  _parentWindow = parentWindow;
  _state = state;
  _finished = NO;
  _completionDelivered = NO;

  constexpr CGFloat kWindowWidth = 1000.0;
  constexpr CGFloat kWindowHeight = 650.0;
  constexpr CGFloat kMargin = 16.0;
  constexpr CGFloat kButtonAreaHeight = 48.0;
  constexpr CGFloat kBrowserWidth = 230.0;

  const NSRect contentRect = NSMakeRect(0.0, 0.0, kWindowWidth, kWindowHeight);
  _sheet = [[NSWindow alloc] initWithContentRect:contentRect
                                       styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskResizable)
                                         backing:NSBackingStoreBuffered
                                           defer:NO];
  _sheet.title = ns_string(QCoreApplication::translate("ScannerImport", "Import from Scanner"));
  _sheet.delegate = self;
  _sheet.contentMinSize = NSMakeSize(900.0, 600.0);

  NSView* contentView = _sheet.contentView;
  NSSplitView* splitView = [[NSSplitView alloc]
      initWithFrame:NSMakeRect(kMargin, kMargin + kButtonAreaHeight,
                               kWindowWidth - (2.0 * kMargin),
                               kWindowHeight - kButtonAreaHeight - (2.0 * kMargin))];
  splitView.vertical = YES;
  splitView.delegate = self;
  splitView.dividerStyle = NSSplitViewDividerStyleThin;
  splitView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

  _browserView = [[IKDeviceBrowserView alloc]
      initWithFrame:NSMakeRect(0.0, 0.0, kBrowserWidth, splitView.bounds.size.height)];
  _browserView.delegate = self;
  _browserView.displaysLocalCameras = NO;
  _browserView.displaysNetworkCameras = NO;
  _browserView.displaysLocalScanners = YES;
  _browserView.displaysNetworkScanners = YES;
  _browserView.mode = IKDeviceBrowserViewDisplayModeOutline;

  _scannerView = [[IKScannerDeviceView alloc]
      initWithFrame:NSMakeRect(kBrowserWidth + 1.0, 0.0,
                               splitView.bounds.size.width - kBrowserWidth - 1.0,
                               splitView.bounds.size.height)];
  _scannerView.delegate = self;
  _scannerView.hasDisplayModeSimple = YES;
  _scannerView.hasDisplayModeAdvanced = YES;
  _scannerView.mode = IKScannerDeviceViewDisplayModeAdvanced;
  _scannerView.transferMode = IKScannerDeviceViewTransferModeFileBased;
  _scannerView.displaysDownloadsDirectoryControl = NO;
  _scannerView.downloadsDirectory =
      [NSURL fileURLWithPath:ns_string(_state->downloads_folder) isDirectory:YES];
  _scannerView.documentName = ns_string(_state->document_name);
  _scannerView.displaysPostProcessApplicationControl = NO;
  _scannerView.postProcessApplication = nil;

  [splitView addSubview:_browserView];
  [splitView addSubview:_scannerView];
  [splitView setPosition:kBrowserWidth ofDividerAtIndex:0];
  [contentView addSubview:splitView];

  NSButton* cancelButton = [NSButton
      buttonWithTitle:ns_string(QCoreApplication::translate("ScannerImport", "Cancel"))
               target:self
               action:@selector(cancelSheet:)];
  cancelButton.bezelStyle = NSBezelStyleRounded;
  cancelButton.keyEquivalent = @"\033";
  [cancelButton sizeToFit];
  NSRect cancelFrame = cancelButton.frame;
  cancelFrame.size.width = std::max<CGFloat>(cancelFrame.size.width + 20.0, 88.0);
  cancelFrame.origin.x = kWindowWidth - kMargin - cancelFrame.size.width;
  cancelFrame.origin.y = kMargin;
  cancelButton.frame = cancelFrame;
  cancelButton.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
  [contentView addSubview:cancelButton];

  return self;
}

- (void)dealloc {
  delete _state;
}

- (void)beginSheet {
  // AppKit retains this completion block for the lifetime of the sheet. Capture the
  // controller strongly so its delegates and C++ completion state stay alive after the
  // asynchronous entry point returns to the application's normal event loop.
  PatchyScannerSheetController* controller = self;
  [_parentWindow beginSheet:_sheet
          completionHandler:^(__unused NSModalResponse returnCode) {
            [controller handleSheetEnded];
          }];
}

- (void)cancelCurrentScanner {
  ICScannerDevice* scanner = _selectedScanner;
  ICScannerFunctionalUnit* functionalUnit = scanner.selectedFunctionalUnit;
  if (functionalUnit != nil && (functionalUnit.scanInProgress || functionalUnit.overviewScanInProgress)) {
    [scanner cancelScan];
  }
  _scannerView.scannerDevice = nil;
  _selectedScanner = nil;
}

- (void)handleSheetEnded {
  if (!_finished && _state != nullptr) {
    _state->result.status = patchy::ui::ScannerAcquireStatus::Cancelled;
    _state->result.file_path.clear();
    _state->result.error.clear();
    _finished = YES;
  }
  _browserView.delegate = nil;
  _scannerView.delegate = nil;
  _sheet.delegate = nil;
  [self cancelCurrentScanner];

  if (_completionDelivered || _state == nullptr) {
    return;
  }
  _completionDelivered = YES;
  remove_scan_files_except(
      _state->downloads_folder, _state->document_name,
      _state->result.status == patchy::ui::ScannerAcquireStatus::Acquired ? _state->result.file_path
                                                                         : QString());
  auto result = std::move(_state->result);
  auto completion = std::move(_state->completion);
  _parentWindow = nil;
  if (completion) {
    completion(std::move(result));
  }
}

- (void)tearDownAndEndSheet {
  _browserView.delegate = nil;
  _scannerView.delegate = nil;
  [self cancelCurrentScanner];

  if (_sheet.sheetParent != nil) {
    [_sheet.sheetParent endSheet:_sheet];
  } else {
    [_sheet orderOut:nil];
    [self handleSheetEnded];
  }
}

- (void)finishSheetImmediately {
  if (_finished) {
    return;
  }
  _finished = YES;
  [self tearDownAndEndSheet];
}

- (void)finishSheetAfterDelegateReturns {
  if (_finished) {
    return;
  }
  _finished = YES;
  PatchyScannerSheetController* controller = self;
  dispatch_async(dispatch_get_main_queue(), ^{
    [controller tearDownAndEndSheet];
  });
}

- (void)finishCancelled {
  if (_finished || _state == nullptr) {
    return;
  }
  _state->result.status = patchy::ui::ScannerAcquireStatus::Cancelled;
  _state->result.file_path.clear();
  _state->result.error.clear();
  [self finishSheetImmediately];
}

- (void)finishFailedWithError:(NSError*)error {
  if (_finished || _state == nullptr || error_is_scan_cancelled(error)) {
    return;
  }
  _state->result.status = patchy::ui::ScannerAcquireStatus::Failed;
  _state->result.file_path.clear();
  _state->result.error = QCoreApplication::translate("ScannerImport", "The scan could not be completed (%1)")
                             .arg(error_description(error));
  [self finishSheetAfterDelegateReturns];
}

- (void)finishFailedWithoutImage {
  if (_finished || _state == nullptr) {
    return;
  }
  _state->result.status = patchy::ui::ScannerAcquireStatus::Failed;
  _state->result.file_path.clear();
  _state->result.error =
      QCoreApplication::translate("ScannerImport", "The scanner did not return an image file.");
  [self finishSheetAfterDelegateReturns];
}

- (void)finishAcquiredPath:(const QString&)path {
  if (_finished || _state == nullptr) {
    QFile::remove(path);
    return;
  }
  _state->result.status = patchy::ui::ScannerAcquireStatus::Acquired;
  _state->result.file_path = path;
  _state->result.error.clear();
  [self finishSheetAfterDelegateReturns];
}

- (void)cancelSheet:(id)sender {
  Q_UNUSED(sender);
  [self finishCancelled];
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
  Q_UNUSED(sender);
  [self finishCancelled];
  return NO;
}

- (void)deviceBrowserView:(IKDeviceBrowserView*)deviceBrowserView selectionDidChange:(ICDevice*)device {
  Q_UNUSED(deviceBrowserView);
  [self cancelCurrentScanner];
  const auto type = device != nil ? static_cast<NSUInteger>(device.type) : 0;
  const auto scannerMask = static_cast<NSUInteger>(ICDeviceTypeMaskScanner);
  if (device != nil && (type & scannerMask) != 0) {
    _selectedScanner = (ICScannerDevice*)device;
    _scannerView.scannerDevice = _selectedScanner;
  }
}

- (CGFloat)splitView:(NSSplitView*)splitView
    constrainMinCoordinate:(CGFloat)proposedMinimumPosition
               ofSubviewAt:(NSInteger)dividerIndex {
  Q_UNUSED(splitView);
  return dividerIndex == 0 ? std::max<CGFloat>(proposedMinimumPosition, 200.0) : proposedMinimumPosition;
}

- (CGFloat)splitView:(NSSplitView*)splitView
    constrainMaxCoordinate:(CGFloat)proposedMaximumPosition
               ofSubviewAt:(NSInteger)dividerIndex {
  if (dividerIndex != 0) {
    return proposedMaximumPosition;
  }
  return std::min<CGFloat>(proposedMaximumPosition, splitView.bounds.size.width - 620.0);
}

- (void)deviceBrowserView:(IKDeviceBrowserView*)deviceBrowserView didEncounterError:(NSError*)error {
  Q_UNUSED(deviceBrowserView);
  [self finishFailedWithError:error];
}

- (void)handleCompletedScanToURL:(NSURL*)url error:(NSError*)error {
  if (error != nil) {
    [self finishFailedWithError:error];
    return;
  }
  const auto path = path_from_url(url);
  if (path.isEmpty() || !QFileInfo(path).isFile()) {
    [self finishFailedWithoutImage];
    return;
  }
  [self finishAcquiredPath:path];
}

- (void)scannerDeviceView:(IKScannerDeviceView*)scannerDeviceView
             didScanToURL:(NSURL*)url
                 fileData:(NSData*)data
                    error:(NSError*)error {
  Q_UNUSED(scannerDeviceView);
  Q_UNUSED(data);
  [self handleCompletedScanToURL:url error:error];
}

- (void)scannerDeviceView:(IKScannerDeviceView*)scannerDeviceView
             didScanToURL:(NSURL*)url
                    error:(NSError*)error {
  Q_UNUSED(scannerDeviceView);
  [self handleCompletedScanToURL:url error:error];
}

- (void)scannerDeviceView:(IKScannerDeviceView*)scannerDeviceView didEncounterError:(NSError*)error {
  Q_UNUSED(scannerDeviceView);
  [self finishFailedWithError:error];
}

@end

namespace patchy::ui {

void acquire_image_from_scanner_async(QWidget* parent, ScannerAcquireCallback callback) {
  if (!callback) {
    return;
  }
  ScannerAcquireResult result;
  if (!platform_is_cocoa()) {
    result.error = QCoreApplication::translate("ScannerImport", "macOS scanner import requires the Cocoa platform.");
    callback(std::move(result));
    return;
  }

  NSWindow* parentWindow = ns_window_for_widget(parent);
  if (parentWindow == nil) {
    result.error = QCoreApplication::translate("ScannerImport", "The scanner window could not be opened.");
    callback(std::move(result));
    return;
  }

  const auto downloadsFolder = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (downloadsFolder.isEmpty() || !QDir().mkpath(downloadsFolder)) {
    result.error = QCoreApplication::translate("ScannerImport", "The temporary scan folder could not be created.");
    callback(std::move(result));
    return;
  }
  const auto documentName = QStringLiteral("patchy-scan-%1-%2")
                                .arg(QCoreApplication::applicationPid())
                                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

  auto* state = new ScannerAsyncState;
  state->completion = std::move(callback);
  state->downloads_folder = downloadsFolder;
  state->document_name = documentName;
  PatchyScannerSheetController* controller =
      [[PatchyScannerSheetController alloc] initWithParentWindow:parentWindow
                                                          state:state];
  if (controller == nil) {
    auto completion = std::move(state->completion);
    delete state;
    result.error = QCoreApplication::translate("ScannerImport", "The scanner window could not be opened.");
    completion(std::move(result));
    return;
  }

  [controller beginSheet];
}

}  // namespace patchy::ui
