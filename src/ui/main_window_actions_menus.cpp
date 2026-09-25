// MainWindow::build_menu_bar_actions(): the menu-bar phase of create_actions()
// (File/Edit/Image/Layer/Type/Select/Filter/Plugins/View/Window/Help menus,
// their hotkeys and connections), split out of main_window_actions.cpp along
// with the anonymous-namespace helpers only this phase uses.
// Pure function move; behavior must stay identical, and the construction order
// is load-bearing (see create_actions() for the phase order).

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/main_window_actions_internal.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/vector_shape.hpp"
#include "core/warp_mesh.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/update_checker.hpp"
#include "ui/zoom_status_bar.hpp"
#include "support/string_utils.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QBuffer>
#include <QButtonGroup>
#include <QByteArray>
#include <QDateTime>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QColorSpace>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLayout>
#include <QResizeEvent>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPolygon>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QScopeGuard>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QMutex>
#include <QRawFont>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tchar.h>
#include <tpcshrd.h>
#endif

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

QString escape_qaction_ampersands(QString text) {
  return text.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

void bind_translated_status_tip(QObject* object, const char* source,
                                const char* context = kMainWindowTranslationContext) {
  if (object == nullptr) {
    return;
  }
  object->setProperty(kTranslationContextProperty, QString::fromLatin1(context));
  object->setProperty(kTranslationStatusTipProperty, QString::fromLatin1(source));
}

}  // namespace

void MainWindow::build_menu_bar_actions(ActionBuildContext& ctx) {
  auto* file_menu = menuBar()->addMenu(tr("&File"));
  auto* edit_menu = menuBar()->addMenu(tr("&Edit"));
  auto* image_menu = menuBar()->addMenu(tr("&Image"));
  auto* layer_menu = menuBar()->addMenu(tr("&Layer"));
  auto* type_menu = menuBar()->addMenu(tr("&Type"));
  auto* select_menu = menuBar()->addMenu(tr("&Select"));
  auto* filter_menu = menuBar()->addMenu(tr("&Filter"));
  auto* plugins_menu = menuBar()->addMenu(tr("&Plugins"));
  auto* view_menu = menuBar()->addMenu(tr("&View"));
  auto* window_menu = menuBar()->addMenu(tr("&Window"));
  auto* help_menu = menuBar()->addMenu(tr("&Help"));
  layer_menu->setObjectName(QStringLiteral("layerMenu"));
  filter_menu->setObjectName(QStringLiteral("filterMenu"));
  bind_action_text(file_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&File"));
  bind_action_text(edit_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Edit"));
  bind_action_text(image_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Image"));
  bind_action_text(layer_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Layer"));
  bind_action_text(type_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Type"));
  bind_action_text(select_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Select"));
  bind_action_text(filter_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Filter"));
  bind_action_text(plugins_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Plugins"));
  bind_action_text(view_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&View"));
  bind_action_text(window_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Window"));
  window_menu->setObjectName(QStringLiteral("windowMenu"));
  bind_action_text(help_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Help"));

  auto* new_action = file_menu->addAction(tr("&New"));
  auto* open_action = file_menu->addAction(tr("&Open..."));
  // A whole folder of images as tabs (the other half of the PDF round trip: pages
  // exported to a folder come back as one document each).
  auto* open_folder_action = file_menu->addAction(tr("Open Fol&der..."));
  bind_action_text(open_folder_action, QT_TR_NOOP("Open Fol&der..."));
  open_folder_action->setObjectName(QStringLiteral("fileOpenFolderAction"));
  register_hotkey(open_folder_action, "file.open_folder");
  connect(open_folder_action, &QAction::triggered, this, [this] { open_folder(); });
  recent_files_menu_ = file_menu->addMenu(tr("Open &Recent File"));
  recent_files_menu_->setObjectName(QStringLiteral("fileOpenRecentMenu"));
  configure_recent_files_context_menu(recent_files_menu_);
  connect(recent_files_menu_, &QMenu::aboutToShow, this, [this] {
    if (recent_files_filter_edit_ != nullptr) {
      // A stale filter from a dismissed menu resets before showing again; the
      // focused edit is what routes typed keys into the filter.
      recent_files_filter_edit_->clear();
      recent_files_filter_edit_->setFocus();
    }
  });
  recent_folders_menu_ = file_menu->addMenu(tr("Open Recent &Folder"));
  recent_folders_menu_->setObjectName(QStringLiteral("fileOpenRecentFolderMenu"));
  configure_recent_files_context_menu(recent_folders_menu_);
  recent_folders_menu_->setProperty(kRecentFoldersMenuProperty, true);
  connect(file_menu, &QMenu::aboutToShow, this, &MainWindow::refresh_recent_history);
  auto* import_menu = file_menu->addMenu(tr("I&mport"));
  bind_widget_text(import_menu, QT_TR_NOOP("I&mport"));
  import_menu->setObjectName(QStringLiteral("fileImportMenu"));
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
#ifdef Q_OS_MACOS
  auto* import_scanner_action = import_menu->addAction(tr("From &Scanner..."));
  bind_action_text(import_scanner_action, QT_TR_NOOP("From &Scanner..."));
#else
  auto* import_scanner_action = import_menu->addAction(tr("From &Scanner or Camera..."));
  bind_action_text(import_scanner_action, QT_TR_NOOP("From &Scanner or Camera..."));
#endif
  import_scanner_action->setObjectName(QStringLiteral("fileImportScannerAction"));
  register_hotkey(import_scanner_action, "file.import_scanner");
  connect(import_scanner_action, &QAction::triggered, this, [this] { import_from_scanner(); });
#ifdef Q_OS_MACOS
  auto* photocopy_action = import_menu->addAction(tr("&Photocopy (Scanner to Printer)..."));
  bind_action_text(photocopy_action, QT_TR_NOOP("&Photocopy (Scanner to Printer)..."));
#else
  auto* photocopy_action = import_menu->addAction(tr("&Photocopy (Scanner or Camera to Printer)..."));
  bind_action_text(photocopy_action, QT_TR_NOOP("&Photocopy (Scanner or Camera to Printer)..."));
#endif
  photocopy_action->setObjectName(QStringLiteral("fileImportPhotocopyAction"));
  register_hotkey(photocopy_action, "file.import_photocopy");
  connect(photocopy_action, &QAction::triggered, this, [this] { photocopy_from_scanner(); });
  auto* divide_scan_action = import_menu->addAction(tr("Scan and &Divide Photos..."));
  bind_action_text(divide_scan_action, QT_TR_NOOP("Scan and &Divide Photos..."));
  divide_scan_action->setObjectName(QStringLiteral("fileImportDivideScannedAction"));
  register_hotkey(divide_scan_action, "file.import_divide_photos");
  connect(divide_scan_action, &QAction::triggered, this, [this] { import_and_divide_from_scanner(); });
#endif
  auto* import_sprite_sheet_action = import_menu->addAction(tr("Sprite Sheet to &Layers..."));
  bind_action_text(import_sprite_sheet_action, QT_TR_NOOP("Sprite Sheet to &Layers..."));
  import_sprite_sheet_action->setObjectName(QStringLiteral("fileImportSpriteSheetAction"));
  register_hotkey(import_sprite_sheet_action, "file.import_sprite_sheet");
  connect(import_sprite_sheet_action, &QAction::triggered, this, [this] { import_sprite_sheet(); });
  auto* import_image_sequence_action = import_menu->addAction(tr("&Image Sequence to Layers..."));
  bind_action_text(import_image_sequence_action, QT_TR_NOOP("&Image Sequence to Layers..."));
  import_image_sequence_action->setObjectName(QStringLiteral("fileImportImageSequenceAction"));
  register_hotkey(import_image_sequence_action, "file.import_image_sequence");
  connect(import_image_sequence_action, &QAction::triggered, this, [this] { import_image_sequence(); });
  auto* import_files_as_layers_action = import_menu->addAction(tr("&Files as Layers..."));
  bind_action_text(import_files_as_layers_action, QT_TR_NOOP("&Files as Layers..."));
  import_files_as_layers_action->setObjectName(QStringLiteral("fileImportFilesAsLayersAction"));
  register_hotkey(import_files_as_layers_action, "file.import_files_as_layers");
  connect(import_files_as_layers_action, &QAction::triggered, this, [this] { import_files_as_layers(); });
  register_document_action(import_files_as_layers_action);
  auto* place_embedded_action = file_menu->addAction(tr("Place &Embedded..."));
  bind_action_text(place_embedded_action, QT_TR_NOOP("Place &Embedded..."));
  place_embedded_action->setObjectName(QStringLiteral("filePlaceEmbeddedAction"));
  register_hotkey(place_embedded_action, "file.place_embedded");
  connect(place_embedded_action, &QAction::triggered, this, [this] { place_embedded_file(); });
  register_document_action(place_embedded_action);
  auto* save_action = file_menu->addAction(tr("&Save"));
  auto* save_as_action = file_menu->addAction(tr("Save &As..."));
  // The export commands sit in their own submenu, the way Import does: the verb
  // belongs to the submenu title so no row has to repeat it.
  auto* export_menu = file_menu->addMenu(tr("E&xport"));
  bind_widget_text(export_menu, QT_TR_NOOP("E&xport"));
  export_menu->setObjectName(QStringLiteral("fileExportMenu"));
  auto* export_flat_action = export_menu->addAction(tr("&Flat Image..."));
  // Several open documents, or the top-level groups of this one, as the pages of one
  // PDF (issue 19: heavy print jobs need real pages, not page-per-layer stacks).
  auto* export_multipage_pdf_action = export_menu->addAction(tr("&Multi-Page PDF..."));
  bind_action_text(export_multipage_pdf_action, QT_TR_NOOP("&Multi-Page PDF..."));
  export_multipage_pdf_action->setObjectName(QStringLiteral("fileExportMultiPagePdfAction"));
  register_hotkey(export_multipage_pdf_action, "file.export_multipage_pdf");
  connect(export_multipage_pdf_action, &QAction::triggered, this, [this] { export_multipage_pdf(); });
  register_document_action(export_multipage_pdf_action);
  // The checked open documents as numbered image files in a folder.
  auto* export_documents_folder_action = export_menu->addAction(tr("&Documents to Folder..."));
  bind_action_text(export_documents_folder_action, QT_TR_NOOP("&Documents to Folder..."));
  export_documents_folder_action->setObjectName(QStringLiteral("fileExportDocumentsToFolderAction"));
  register_hotkey(export_documents_folder_action, "file.export_documents_to_folder");
  connect(export_documents_folder_action, &QAction::triggered, this, [this] { export_documents_to_folder(); });
  register_document_action(export_documents_folder_action);
  // Above the line: whole documents. Below it: the layers of the active one.
  export_menu->addSeparator();
  auto* export_sprite_sheet_action = export_menu->addAction(tr("Layers as Sprite S&heet..."));
  bind_action_text(export_sprite_sheet_action, QT_TR_NOOP("Layers as Sprite S&heet..."));
  export_sprite_sheet_action->setObjectName(QStringLiteral("fileExportSpriteSheetAction"));
  register_hotkey(export_sprite_sheet_action, "file.export_sprite_sheet");
  connect(export_sprite_sheet_action, &QAction::triggered, this, [this] { export_sprite_sheet(); });
  register_document_action(export_sprite_sheet_action);
  auto* export_image_sequence_action = export_menu->addAction(tr("Layers as Image Se&quence..."));
  bind_action_text(export_image_sequence_action, QT_TR_NOOP("Layers as Image Se&quence..."));
  export_image_sequence_action->setObjectName(QStringLiteral("fileExportImageSequenceAction"));
  register_hotkey(export_image_sequence_action, "file.export_image_sequence");
  connect(export_image_sequence_action, &QAction::triggered, this, [this] { export_image_sequence(); });
  register_document_action(export_image_sequence_action);
  // Stays visible on wasm, unlike the sequence export below: an animation is one file,
  // so the browser handoff is a single download.
  auto* export_animated_gif_action = export_menu->addAction(tr("Layers as Animated &GIF..."));
  bind_action_text(export_animated_gif_action, QT_TR_NOOP("Layers as Animated &GIF..."));
  export_animated_gif_action->setObjectName(QStringLiteral("fileExportAnimatedGifAction"));
  register_hotkey(export_animated_gif_action, "file.export_animated_gif");
  connect(export_animated_gif_action, &QAction::triggered, this, [this] { export_animated_gif(); });
  register_document_action(export_animated_gif_action);
  auto* page_setup_action = file_menu->addAction(tr("Page Set&up..."));
  auto* print_action = file_menu->addAction(tr("&Print..."));
#ifdef Q_OS_WASM
  // Qt ships no QtPrintSupport for WebAssembly (print_dialog_wasm.cpp stubs the
  // dialogs), and an image-sequence export would mean one browser download per
  // layer, which browsers refuse as download spam. The actions stay registered
  // so hotkey ids and wiring are stable; hidden actions do not render in the
  // menu. A browser has no host folders to open or write into either.
  export_image_sequence_action->setVisible(false);
  // A browser pick is a MEMFS transfer path that only open_document_path releases.
  import_files_as_layers_action->setVisible(false);
  export_documents_folder_action->setVisible(false);
  open_folder_action->setVisible(false);
  page_setup_action->setVisible(false);
  print_action->setVisible(false);
#endif
  file_menu->addSeparator();
  // File > Scripts: static entries here (hotkeys register once); the script
  // entries themselves rescan on every open (main_window_scripting.cpp).
  scripts_menu_ = file_menu->addMenu(tr("Scrip&ts"));
  scripts_menu_->setObjectName(QStringLiteral("fileScriptsMenu"));
  bind_action_text(scripts_menu_->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Scrip&ts"));
  // Display text says Manager; the command id and object name keep the
  // historical "editor" spelling (persisted identifiers, never renamed).
  auto* script_editor_action = scripts_menu_->addAction(tr("Script &Manager..."));
  script_editor_action->setObjectName(QStringLiteral("fileScriptEditorAction"));
  register_hotkey(script_editor_action, "file.scripts.editor");
  connect(script_editor_action, &QAction::triggered, this, [this] { open_script_editor(); });
  bind_action_text(script_editor_action, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Script &Manager..."));
  auto* browse_scripts_action = scripts_menu_->addAction(tr("&Browse Scripts Folder..."));
  browse_scripts_action->setObjectName(QStringLiteral("fileBrowseScriptsFolderAction"));
  register_hotkey(browse_scripts_action, "file.scripts.browse_folder");
  connect(browse_scripts_action, &QAction::triggered, this, [this] { browse_user_scripts_folder(); });
  bind_action_text(browse_scripts_action, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Browse Scripts Folder..."));
  scripts_menu_->addSeparator();
  connect(scripts_menu_, &QMenu::aboutToShow, this, [this] { rebuild_scripts_menu(); });
  file_menu->addSeparator();
  auto* close_action = file_menu->addAction(tr("&Close"));
  auto* close_all_action = file_menu->addAction(tr("Close &All"));
  file_menu->addSeparator();
  auto* preferences_action = file_menu->addAction(tr("&Preferences..."));
  file_menu->addSeparator();
  auto* quit_action = file_menu->addAction(tr("&Quit"));
  new_action->setObjectName(QStringLiteral("fileNewAction"));
  open_action->setObjectName(QStringLiteral("fileOpenAction"));
  save_action->setObjectName(QStringLiteral("fileSaveAction"));
  save_as_action->setObjectName(QStringLiteral("fileSaveAsAction"));
  export_flat_action->setObjectName(QStringLiteral("fileExportFlatAction"));
  page_setup_action->setObjectName(QStringLiteral("filePageSetupAction"));
  print_action->setObjectName(QStringLiteral("filePrintAction"));
  close_action->setObjectName(QStringLiteral("fileCloseAction"));
  close_all_action->setObjectName(QStringLiteral("fileCloseAllAction"));
  preferences_action->setObjectName(QStringLiteral("filePreferencesAction"));
  new_action->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
  open_action->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  save_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  save_as_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  export_flat_action->setIcon(style()->standardIcon(QStyle::SP_DriveHDIcon));
  page_setup_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  print_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
  close_action->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
  close_all_action->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
  preferences_action->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  register_hotkey(new_action, "file.new", QKeySequence(Qt::CTRL | Qt::Key_N));
  register_hotkey(open_action, "file.open", QKeySequence(Qt::CTRL | Qt::Key_O));
  register_hotkey(save_action, "file.save", QKeySequence(Qt::CTRL | Qt::Key_S));
  register_hotkey(save_as_action, "file.save_as", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  register_hotkey(print_action, "file.print", QKeySequence(Qt::CTRL | Qt::Key_P));
  register_hotkey(close_action, "file.close", QKeySequence(Qt::CTRL | Qt::Key_W));
  register_hotkey(close_all_action, "file.close_all", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
  // macOS relocation into the app menu (About / Settings… / Quit) happens via the menu
  // roles; Qt ignores the roles everywhere else, so they are set unconditionally.
  preferences_action->setMenuRole(QAction::PreferencesRole);
  quit_action->setMenuRole(QAction::QuitRole);
  register_hotkey(quit_action, "file.quit", QKeySequence(Qt::CTRL | Qt::Key_Q));
  // Ctrl+Alt+Shift+S is Photoshop's Save for Web key (Qt maps CTRL onto Command on macOS).
  register_hotkey(export_flat_action, "file.export_flat",
                  QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_S));
  register_hotkey(page_setup_action, "file.page_setup");
  // Cmd+, is the universal macOS settings shortcut (Qt maps CTRL to Cmd there);
  // Windows/Linux keep Preferences unbound by default.
  register_hotkey(preferences_action, "file.preferences",
                  platform_hotkey(QKeySequence(), QKeySequence(Qt::CTRL | Qt::Key_Comma)));

  connect(new_action, &QAction::triggered, this, [this] { create_new_document(); });
  connect(open_action, &QAction::triggered, this, [this] { open_document(); });
  connect(save_action, &QAction::triggered, this, [this] { save_document(); });
  connect(save_as_action, &QAction::triggered, this, [this] { save_document_as(); });
  connect(export_flat_action, &QAction::triggered, this, [this] { export_flat_image(); });
  connect(page_setup_action, &QAction::triggered, this, [this] { page_setup(); });
  connect(print_action, &QAction::triggered, this, [this] { print_document(); });
  connect(close_action, &QAction::triggered, this, [this] { close_active_document(); });
  connect(close_all_action, &QAction::triggered, this, [this] { close_all_document_tabs(); });
  connect(preferences_action, &QAction::triggered, this, [this] { show_preferences(); });
  connect(quit_action, &QAction::triggered, this, &QWidget::close);
  for (auto* action :
       {save_action, save_as_action, export_flat_action, page_setup_action, print_action, close_action, close_all_action}) {
    register_document_action(action);
  }

  undo_action_ = edit_menu->addAction(tr("&Undo"));
  redo_action_ = edit_menu->addAction(tr("&Redo"));
  undo_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
  redo_action_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
  // Ctrl+Alt+Z is Photoshop's "step backward" muscle memory; Ctrl+Y matches Windows redo.
  register_hotkey(undo_action_, "edit.undo",
                  QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z)});
  register_hotkey(redo_action_, "edit.redo",
                  QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y)});
  connect(undo_action_, &QAction::triggered, this, [this] { undo(); });
  connect(redo_action_, &QAction::triggered, this, [this] { redo(); });
  edit_menu->addSeparator();
  auto* cut_action = edit_menu->addAction(tr("Cu&t"));
  auto* copy_action = edit_menu->addAction(tr("&Copy"));
  auto* copy_merged_action = edit_menu->addAction(tr("Copy Merged"));
  auto* paste_action = edit_menu->addAction(tr("&Paste"));
  auto* paste_in_place_action = edit_menu->addAction(tr("Paste in Place"));
  paste_in_place_action->setObjectName(QStringLiteral("editPasteInPlaceAction"));
  paste_in_place_action->setProperty("patchy.channelViewBlocked", true);
  bind_action_text(paste_in_place_action, QT_TR_NOOP("Paste in Place"));
  auto* transform_action = edit_menu->addAction(tr("Free &Transform..."));
  auto* warp_transform_action = edit_menu->addAction(tr("Warp Transform"));
  bind_action_text(warp_transform_action, QT_TR_NOOP("Warp Transform"));
  cut_action->setObjectName(QStringLiteral("editCutAction"));
  copy_action->setObjectName(QStringLiteral("editCopyAction"));
  copy_merged_action->setObjectName(QStringLiteral("editCopyMergedAction"));
  paste_action->setObjectName(QStringLiteral("editPasteAction"));
  transform_action->setObjectName(QStringLiteral("editFreeTransformAction"));
  free_transform_action_ = transform_action;
  warp_transform_action->setObjectName(QStringLiteral("editWarpTransformAction"));
  cut_action->setIcon(simple_icon(QStringLiteral("CT")));
  copy_action->setIcon(simple_icon(QStringLiteral("CP")));
  copy_merged_action->setIcon(simple_icon(QStringLiteral("CM")));
  paste_action->setIcon(simple_icon(QStringLiteral("paste")));
  paste_in_place_action->setIcon(simple_icon(QStringLiteral("paste")));
  transform_action->setIcon(simple_icon(QStringLiteral("TR")));
  warp_transform_action->setIcon(simple_icon(QStringLiteral("WP")));
  register_hotkey(cut_action, "edit.cut", QKeySequence(Qt::CTRL | Qt::Key_X));
  register_hotkey(copy_action, "edit.copy", QKeySequence(Qt::CTRL | Qt::Key_C));
  register_hotkey(copy_merged_action, "edit.copy_merged", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
  register_hotkey(paste_action, "edit.paste", QKeySequence(Qt::CTRL | Qt::Key_V));
  register_hotkey(paste_in_place_action, "edit.paste_in_place", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
  register_hotkey(transform_action, "edit.free_transform", QKeySequence(Qt::CTRL | Qt::Key_T));
  register_hotkey(warp_transform_action, "edit.warp_transform", QKeySequence());
  connect(cut_action, &QAction::triggered, this, [this] { cut_selection(); });
  connect(copy_action, &QAction::triggered, this, [this] { copy_selection(); });
  connect(copy_merged_action, &QAction::triggered, this, [this] { copy_merged(); });
  auto* copy_svg_action = new QAction(tr("Copy as SVG"), this);
  copy_svg_action->setObjectName(QStringLiteral("editCopySvgAction"));
  copy_svg_action->setProperty("patchy.channelViewBlocked", true);
  copy_svg_action->setStatusTip(tr("Copy the selected layers to the clipboard as SVG"));
  bind_action_text(copy_svg_action, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Copy as SVG"));
  bind_translated_status_tip(copy_svg_action, "Copy the selected layers to the clipboard as SVG");
  apply_bound_translation(copy_svg_action);
  edit_menu->insertAction(paste_action, copy_svg_action);
  register_hotkey(copy_svg_action, "edit.copy_svg");
  connect(copy_svg_action, &QAction::triggered, this, [this] { copy_as_svg(); });
  register_document_action(copy_svg_action);
  connect(paste_action, &QAction::triggered, this, [this] { paste_clipboard(); });
  connect(paste_in_place_action, &QAction::triggered, this, [this] { paste_clipboard(true); });
  connect(transform_action, &QAction::triggered, this, [this] { transform_active_layer_dialog(); });
  connect(warp_transform_action, &QAction::triggered, this, [this] { warp_transform_active_layer(); });
  for (auto* action : {cut_action, copy_action, copy_merged_action, paste_action, paste_in_place_action, transform_action,
                       warp_transform_action}) {
    register_document_action(action);
  }
  edit_menu->addSeparator();
  auto* select_all_action = edit_menu->addAction(tr("Select &All"));
  auto* clear_selection_action = edit_menu->addAction(tr("&Clear Selection"));
  auto* reselect_action = edit_menu->addAction(tr("&Reselect"));
  auto* inverse_selection_action = edit_menu->addAction(tr("&Inverse"));
  auto* deselect_layers_action = new QAction(tr("Deselect La&yers"), this);
  deselect_layers_action->setObjectName(QStringLiteral("selectDeselectLayersAction"));
  bind_action_text(deselect_layers_action, QT_TR_NOOP("Deselect La&yers"));
  deselect_layers_action->setIcon(simple_icon(QStringLiteral("DL")));
  register_hotkey(deselect_layers_action, "select.deselect_layers");
  connect(deselect_layers_action, &QAction::triggered, this, [this] { deselect_all_layers(); });
  register_document_action(deselect_layers_action);
  quick_mask_action_ = new QAction(tr("Edit in &Quick Mask Mode"), this);
  quick_mask_action_->setObjectName(QStringLiteral("selectQuickMaskAction"));
  quick_mask_action_->setCheckable(true);
  quick_mask_action_->setIcon(
      simple_icon(QStringLiteral("QM"), QColor(235, 95, 110)));
  bind_action_text(quick_mask_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Edit in &Quick Mask Mode"));
  auto* grow_selection_action = new QAction(tr("&Grow"), this);
  auto* similar_selection_action = new QAction(tr("Simi&lar"), this);
  auto* expand_selection_action = new QAction(tr("&Expand..."), this);
  auto* contract_selection_action = new QAction(tr("Con&tract..."), this);
  auto* border_selection_action = new QAction(tr("&Border..."), this);
  auto* layer_transparency_action = new QAction(tr("Load Layer &Transparency"), this);
  auto* stroke_selection_action = edit_menu->addAction(tr("&Stroke Selection..."));
  // Remove Object: the content-aware exemplar fill of the selection through
  // the Remove Object dialog (Reroll, Tone match, Edge feather;
  // main_window_layer_ops.cpp). Also the first entry of the canvas context
  // menu's selection section and the Patch options bar's button. The
  // nearest-edge mirror has no menu entry (Seth, September 2026: confusing
  // next to this one); it stays as the automatic fallback and the script
  // API's "nearestEdge" method.
  auto* remove_object_action = edit_menu->addAction(tr("Remove &Object..."));
  remove_object_action->setObjectName(QStringLiteral("editRemoveObjectAction"));
  remove_object_action->setIcon(simple_icon(QStringLiteral("RO")));
  register_hotkey(remove_object_action, "edit.remove_object");
  connect(remove_object_action, &QAction::triggered, this, [this] { remove_object_dialog(); });
  auto* define_brush_tip_action = edit_menu->addAction(tr("Define Brush Tip from Selection"));
  define_brush_tip_action->setObjectName(QStringLiteral("editDefineBrushTipAction"));
  register_hotkey(define_brush_tip_action, "edit.define_brush_tip");
  connect(define_brush_tip_action, &QAction::triggered, this, [this] { define_brush_tip_from_selection(); });
  register_document_action(define_brush_tip_action);
  auto* define_custom_shape_action = edit_menu->addAction(tr("Define Custom Shape from Path"));
  bind_action_text(define_custom_shape_action, QT_TR_NOOP("Define Custom Shape from Path"));
  define_custom_shape_action->setObjectName(QStringLiteral("editDefineCustomShapeAction"));
  define_custom_shape_action_ = define_custom_shape_action;
  register_hotkey(define_custom_shape_action, "edit.define_custom_shape");
  connect(define_custom_shape_action, &QAction::triggered, this,
          [this] { define_custom_shape_from_path(); });
  register_document_action(define_custom_shape_action);
  // Needs no document (the shape lands in the application library), so it is
  // deliberately not a document action - the Photoshop Shapes-panel import.
  auto* define_custom_shape_svg_action = edit_menu->addAction(tr("Define Custom Shape from SVG File"));
  bind_action_text(define_custom_shape_svg_action, QT_TR_NOOP("Define Custom Shape from SVG File"));
  define_custom_shape_svg_action->setObjectName(QStringLiteral("editDefineCustomShapeFromSvgAction"));
  register_hotkey(define_custom_shape_svg_action, "edit.define_custom_shape_svg");
  connect(define_custom_shape_svg_action, &QAction::triggered, this,
          [this] { define_custom_shape_from_svg_file(); });
  select_all_action->setObjectName(QStringLiteral("editSelectAllAction"));
  clear_selection_action->setObjectName(QStringLiteral("editDeselectAction"));
  reselect_action->setObjectName(QStringLiteral("selectReselectAction"));
  inverse_selection_action->setObjectName(QStringLiteral("selectInverseAction"));
  grow_selection_action->setObjectName(QStringLiteral("selectGrowAction"));
  similar_selection_action->setObjectName(QStringLiteral("selectSimilarAction"));
  expand_selection_action->setObjectName(QStringLiteral("selectExpandAction"));
  contract_selection_action->setObjectName(QStringLiteral("selectContractAction"));
  border_selection_action->setObjectName(QStringLiteral("selectBorderAction"));
  layer_transparency_action->setObjectName(QStringLiteral("selectLayerTransparencyAction"));
  stroke_selection_action->setObjectName(QStringLiteral("editStrokeSelectionAction"));
  select_all_action->setIcon(simple_icon(QStringLiteral("SA")));
  clear_selection_action->setIcon(simple_icon(QStringLiteral("DS")));
  reselect_action->setIcon(simple_icon(QStringLiteral("RS")));
  inverse_selection_action->setIcon(simple_icon(QStringLiteral("INV")));
  grow_selection_action->setIcon(simple_icon(QStringLiteral("GR")));
  similar_selection_action->setIcon(simple_icon(QStringLiteral("SIM")));
  expand_selection_action->setIcon(simple_icon(QStringLiteral("EXP")));
  contract_selection_action->setIcon(simple_icon(QStringLiteral("CTR")));
  border_selection_action->setIcon(simple_icon(QStringLiteral("BD")));
  layer_transparency_action->setIcon(simple_icon(QStringLiteral("AL")));
  stroke_selection_action->setIcon(simple_icon(QStringLiteral("stroke")));
  register_hotkey(select_all_action, "select.all", QKeySequence(Qt::CTRL | Qt::Key_A));
  register_hotkey(clear_selection_action, "select.deselect", QKeySequence(Qt::CTRL | Qt::Key_D));
  register_hotkey(reselect_action, "select.reselect", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  register_hotkey(inverse_selection_action, "select.inverse", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
  register_hotkey(quick_mask_action_, "select.quick_mask",
                  QKeySequence(Qt::Key_Q));
  register_hotkey(grow_selection_action, "select.grow");
  register_hotkey(similar_selection_action, "select.similar");
  register_hotkey(expand_selection_action, "select.expand");
  register_hotkey(contract_selection_action, "select.contract");
  register_hotkey(border_selection_action, "select.border");
  register_hotkey(layer_transparency_action, "select.layer_transparency");
  register_hotkey(stroke_selection_action, "edit.stroke_selection");
  connect(select_all_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Select All"), [this] { canvas_->select_all(); }); });
  connect(clear_selection_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Deselect"), [this] { canvas_->clear_selection(); }); });
  connect(reselect_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Reselect"), [this] { canvas_->reselect(); }); });
  connect(inverse_selection_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Inverse Selection"), [this] { canvas_->invert_selection(); }); });
  connect(quick_mask_action_, &QAction::triggered, this,
          [this] { toggle_quick_mask_mode(); });
  connect(grow_selection_action, &QAction::triggered, this,
          [this] { canvas_->run_selection_command(tr("Grow Selection"), [this] { canvas_->grow_selection(); }); });
  connect(similar_selection_action, &QAction::triggered, this, [this] {
    canvas_->run_selection_command(tr("Select Similar"), [this] { canvas_->select_similar_to_selection(); });
  });
  connect(expand_selection_action, &QAction::triggered, this, [this] { expand_selection_dialog(); });
  connect(contract_selection_action, &QAction::triggered, this, [this] { contract_selection_dialog(); });
  connect(border_selection_action, &QAction::triggered, this, [this] { border_selection_dialog(); });
  connect(layer_transparency_action, &QAction::triggered, this, [this] {
    canvas_->run_selection_command(tr("Load Layer Transparency"),
                                   [this] { canvas_->select_active_layer_opaque_pixels(); });
  });
  connect(stroke_selection_action, &QAction::triggered, this, [this] { stroke_selection(); });
  for (auto* action : {select_all_action, clear_selection_action, reselect_action, inverse_selection_action,
                       quick_mask_action_,
                       grow_selection_action, similar_selection_action, expand_selection_action,
                       contract_selection_action, border_selection_action, layer_transparency_action,
                       stroke_selection_action, remove_object_action}) {
    register_document_action(action);
  }
  for (auto* action : {select_all_action, clear_selection_action, reselect_action,
                       grow_selection_action, similar_selection_action,
                       expand_selection_action, contract_selection_action,
                       border_selection_action, layer_transparency_action,
                       stroke_selection_action, remove_object_action}) {
    action->setProperty("patchy.quickMaskBlocked", true);
  }
  select_menu->addAction(select_all_action);
  select_menu->addAction(clear_selection_action);
  select_menu->addAction(reselect_action);
  select_menu->addAction(inverse_selection_action);
  select_menu->addAction(deselect_layers_action);
  select_menu->addSeparator();
  select_menu->addAction(quick_mask_action_);
  select_menu->addSeparator();
  select_menu->addAction(grow_selection_action);
  select_menu->addAction(similar_selection_action);
  select_menu->addAction(expand_selection_action);
  select_menu->addAction(contract_selection_action);
  select_menu->addAction(border_selection_action);
  select_menu->addAction(layer_transparency_action);
  select_menu->addSeparator();
  select_menu->addAction(stroke_selection_action);
  select_menu->addAction(remove_object_action);

  // The Layer menu groups the new-layer, mask, and arrange sets into submenus
  // so the whole menu fits a short browser viewport in the wasm build;
  // ui_main_window_renders_color_controls bounds its direct row count. The
  // Layer Mask submenu mirrors the layer context menu's order and separators.
  auto* layer_new_menu = layer_menu->addMenu(tr("&New"));
  layer_new_menu->setObjectName(QStringLiteral("layerNewMenu"));
  auto* add_layer_action = layer_new_menu->addAction(tr("&New Layer"));
  auto* add_folder_action = layer_new_menu->addAction(tr("New &Folder"));
  auto* ungroup_action = layer_new_menu->addAction(tr("Ungroup Layers"));
  ungroup_action->setObjectName(QStringLiteral("layerUngroupAction"));
  ungroup_action->setProperty("patchy.channelViewBlocked", true);
  ungroup_action->setStatusTip(tr("Release the selected folder's layers into their parent"));
  bind_action_text(ungroup_action, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Ungroup Layers"));
  bind_translated_status_tip(ungroup_action, "Release the selected folder's layers into their parent");
  apply_bound_translation(ungroup_action);
  register_hotkey(ungroup_action, "layer.ungroup", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
  connect(ungroup_action, &QAction::triggered, this, [this] { ungroup_selected_layers(); });
  register_document_action(ungroup_action);
  layer_new_menu->addSeparator();
  auto* layer_via_copy_action = layer_new_menu->addAction(tr("Layer Via &Copy"));
  auto* layer_via_cut_action = layer_new_menu->addAction(tr("Layer Via Cu&t"));
  layer_new_menu->addSeparator();
  // Inside the New submenu (it makes layers from the current one, like Layer
  // Via Copy) so the Layer menu keeps its wasm-viewport row bound.
  auto* trace_image_action = layer_new_menu->addAction(tr("Trace Image to Shapes..."));
  layer_trace_image_action_ = trace_image_action;  // the Layers panel context menu reuses it
  trace_image_action->setObjectName(QStringLiteral("layerTraceImageAction"));
  trace_image_action->setProperty("patchy.channelViewBlocked", true);
  trace_image_action->setIcon(simple_icon(QStringLiteral("TR"), QColor(170, 230, 190)));
  trace_image_action->setStatusTip(tr("Convert the pixel layer into editable shape layers, one per color"));
  bind_action_text(trace_image_action, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Trace Image to Shapes..."));
  bind_translated_status_tip(trace_image_action,
                             "Convert the pixel layer into editable shape layers, one per color");
  apply_bound_translation(trace_image_action);
  refresh_action_tooltip(trace_image_action);
  register_hotkey(trace_image_action, "layer.trace_image_to_shapes",
                  QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_T));
  connect(trace_image_action, &QAction::triggered, this, [this] { trace_image_to_shapes(); });
  register_document_action(trace_image_action);
  auto* new_adjustment_layer_menu = layer_menu->addMenu(tr("New &Adjustment Layer"));
  new_adjustment_layer_menu->setObjectName(QStringLiteral("layerNewAdjustmentMenu"));
  populate_new_adjustment_layer_menu(new_adjustment_layer_menu, QStringLiteral("layerNew"));
  auto* new_fill_layer_menu = layer_menu->addMenu(tr("New F&ill Layer"));
  new_fill_layer_menu->setObjectName(QStringLiteral("layerNewFillMenu"));
  populate_new_fill_layer_menu(new_fill_layer_menu, QStringLiteral("layerNew"));
  layer_menu->addSeparator();
  auto* layer_mask_menu = layer_menu->addMenu(tr("Layer Mask"));
  layer_mask_menu->setObjectName(QStringLiteral("layerMaskMenu"));
  auto* add_mask_action = layer_mask_menu->addAction(tr("Add Layer &Mask"));
  edit_layer_mask_action_ = layer_mask_menu->addAction(tr("&Edit Layer Mask"));
  mask_overlay_action_ = layer_mask_menu->addAction(tr("Show Mask &Overlay"));
  view_layer_mask_action_ = layer_mask_menu->addAction(tr("View Layer Mask"));
  layer_mask_menu->addSeparator();
  link_layer_mask_action_ = layer_mask_menu->addAction(tr("Link Layer &Mask"));
  disable_layer_mask_action_ = layer_mask_menu->addAction(tr("&Disable Layer Mask"));
  invert_layer_mask_action_ = layer_mask_menu->addAction(tr("&Invert Layer Mask"));
  layer_mask_menu->addSeparator();
  apply_layer_mask_action_ = layer_mask_menu->addAction(tr("&Apply Layer Mask"));
  delete_layer_mask_action_ = layer_mask_menu->addAction(tr("&Delete Layer Mask"));
  layer_clipping_mask_action_ = layer_menu->addAction(tr("Create Clipping Mask"));
  bind_action_text(layer_clipping_mask_action_, QT_TR_NOOP("Create Clipping Mask"));
  auto* vector_mask_menu = layer_menu->addMenu(tr("&Vector Mask"));
  vector_mask_menu->setObjectName(QStringLiteral("layerVectorMaskMenu"));
  populate_vector_mask_menu(vector_mask_menu, QStringLiteral("layer"));
  layer_menu->addSeparator();
  auto* edit_adjustment_action = layer_menu->addAction(tr("&Edit Adjustment..."));
  layer_blending_options_action_ = layer_menu->addAction(tr("Edit Layer &Styles..."));
  layer_copy_style_action_ = new QAction(tr("Copy Layer Style"), this);
  layer_paste_style_action_ = new QAction(tr("Paste Layer Style"), this);
  layer_delete_style_action_ = new QAction(tr("Delete Layer Style"), this);
  layer_rasterize_action_ = new QAction(tr("Rasterize"), this);
  layer_rasterize_layer_style_action_ = new QAction(tr("Rasterize (including layer style)"), this);
  layer_convert_smart_object_action_ = new QAction(tr("Convert to Smart Object"), this);
  bind_action_text(layer_convert_smart_object_action_, QT_TR_NOOP("Convert to Smart Object"));
  layer_smart_object_edit_action_ = new QAction(tr("Edit Smart Object Contents"), this);
  bind_action_text(layer_smart_object_edit_action_, QT_TR_NOOP("Edit Smart Object Contents"));
  layer_smart_object_replace_action_ = new QAction(tr("Replace Smart Object Contents..."), this);
  bind_action_text(layer_smart_object_replace_action_, QT_TR_NOOP("Replace Smart Object Contents..."));
  layer_smart_object_export_action_ = new QAction(tr("Export Smart Object Contents..."), this);
  bind_action_text(layer_smart_object_export_action_, QT_TR_NOOP("Export Smart Object Contents..."));
  layer_smart_object_via_copy_action_ = new QAction(tr("New Smart Object via Copy"), this);
  bind_action_text(layer_smart_object_via_copy_action_, QT_TR_NOOP("New Smart Object via Copy"));
  layer_smart_object_update_action_ = new QAction(tr("Update Smart Object Content"), this);
  bind_action_text(layer_smart_object_update_action_, QT_TR_NOOP("Update Smart Object Content"));
  layer_smart_object_relink_action_ = new QAction(tr("Relink to File..."), this);
  bind_action_text(layer_smart_object_relink_action_, QT_TR_NOOP("Relink to File..."));
  layer_smart_object_embed_action_ = new QAction(tr("Embed Linked Smart Object"), this);
  bind_action_text(layer_smart_object_embed_action_, QT_TR_NOOP("Embed Linked Smart Object"));
  // The same operation as Rasterize, named so people who don't know the term can
  // still find "make this a plain layer again" where they look for it.
  layer_smart_object_to_normal_action_ = new QAction(tr("Convert to Normal Layer (Rasterize)"), this);
  bind_action_text(layer_smart_object_to_normal_action_, QT_TR_NOOP("Convert to Normal Layer (Rasterize)"));
  auto* layer_smart_objects_menu = layer_menu->addMenu(tr("Smart Objects"));
  layer_smart_objects_menu->setObjectName(QStringLiteral("layerSmartObjectsMenu"));
  layer_smart_objects_menu->addAction(layer_convert_smart_object_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_edit_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_replace_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_update_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_relink_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_embed_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_export_action_);
  layer_smart_objects_menu->addAction(layer_smart_object_via_copy_action_);
  layer_smart_objects_menu->addSeparator();
  layer_smart_objects_menu->addAction(layer_smart_object_to_normal_action_);
  // Commands on existing shapes (docs/vector-commands.md). A submenu keeps the
  // Layer menu inside its wasm-viewport row bound.
  auto* layer_shape_menu = layer_menu->addMenu(tr("Shape"));
  layer_shape_menu->setObjectName(QStringLiteral("layerShapeMenu"));
  bind_widget_text(layer_shape_menu, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Shape"));
  layer_shape_appearance_action_ = layer_shape_menu->addAction(tr("Shape Appearance..."));
  layer_shape_appearance_action_->setObjectName(QStringLiteral("layerShapeAppearanceAction"));
  layer_shape_appearance_action_->setProperty("patchy.channelViewBlocked", true);
  layer_shape_appearance_action_->setStatusTip(tr("Edit the active shape layer's fill, stroke, opacity, and edge"));
  bind_action_text(layer_shape_appearance_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Shape Appearance..."));
  bind_translated_status_tip(layer_shape_appearance_action_,
                             "Edit the active shape layer's fill, stroke, opacity, and edge");
  apply_bound_translation(layer_shape_appearance_action_);
  register_hotkey(layer_shape_appearance_action_, "layer.shape_appearance");
  connect(layer_shape_appearance_action_, &QAction::triggered, this, [this] { edit_active_shape_appearance(); });
  register_document_action(layer_shape_appearance_action_);
  layer_shape_appearance_action_->setEnabled(false);
  layer_shape_menu->addSeparator();
  path_simplify_action_ = layer_shape_menu->addAction(tr("Simplify Path..."));
  path_simplify_action_->setObjectName(QStringLiteral("pathSimplifyAction"));
  path_simplify_action_->setProperty("patchy.channelViewBlocked", true);
  path_simplify_action_->setStatusTip(tr("Refit the targeted path with fewer points"));
  bind_action_text(path_simplify_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Simplify Path..."));
  bind_translated_status_tip(path_simplify_action_, "Refit the targeted path with fewer points");
  apply_bound_translation(path_simplify_action_);
  register_hotkey(path_simplify_action_, "path.simplify");
  connect(path_simplify_action_, &QAction::triggered, this, [this] { simplify_target_path(); });
  register_document_action(path_simplify_action_);
  layer_shape_menu->addSeparator();
  struct CombineEntry {
    const char* text;
    const char* object_name;
    const char* hotkey_id;
    patchy::PathCombineOp op;
  };
  const CombineEntry combine_entries[] = {
      {QT_TR_NOOP("Unite Shapes"), "layerCombineUniteAction", "layer.combine_unite", patchy::PathCombineOp::Add},
      {QT_TR_NOOP("Subtract Front Shape"), "layerCombineSubtractAction", "layer.combine_subtract",
       patchy::PathCombineOp::Subtract},
      {QT_TR_NOOP("Intersect Shapes"), "layerCombineIntersectAction", "layer.combine_intersect",
       patchy::PathCombineOp::Intersect},
      {QT_TR_NOOP("Exclude Overlapping Shapes"), "layerCombineExcludeAction", "layer.combine_exclude",
       patchy::PathCombineOp::Xor},
  };
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& entry = combine_entries[i];
    auto* action = layer_shape_menu->addAction(tr(entry.text));
    action->setObjectName(QLatin1String(entry.object_name));
    action->setProperty("patchy.channelViewBlocked", true);
    bind_action_text(action, entry.text);
    register_hotkey(action, QLatin1String(entry.hotkey_id));
    const auto op = entry.op;
    connect(action, &QAction::triggered, this, [this, op] { combine_selected_shape_layers(op); });
    register_document_action(action);
    action->setEnabled(false);
    layer_combine_actions_[i] = action;
  }
  layer_menu->addSeparator();
  auto* duplicate_layer_action = layer_menu->addAction(tr("&Duplicate Layer"));
  auto* merge_visible_action = layer_menu->addAction(tr("Merge &Visible to New Layer (Copy)"));
  merge_visible_action->setObjectName(QStringLiteral("layerMergeVisibleAction"));
  auto* merge_down_action = layer_menu->addAction(tr("Merge &Down"));
  merge_down_action->setObjectName(QStringLiteral("layerMergeDownAction"));
  auto* rename_layer_action = layer_menu->addAction(tr("&Rename Layer..."));
  rename_layer_action->setObjectName(QStringLiteral("layerRenameAction"));
  auto* delete_layer_action = layer_menu->addAction(tr("&Delete Layer"));
  // (No separator before the fill group: the Layer menu's 23-row bound paid
  // for the Shape submenu with it.)
  auto* fill_layer_action = layer_menu->addAction(tr("&Fill Layer / Selection"));
  auto* fill_background_action = layer_menu->addAction(tr("Fill With &Background Color"));
  auto* clear_layer_action = layer_menu->addAction(tr("&Clear Layer / Selection"));
  layer_menu->addSeparator();
  auto* layer_arrange_menu = layer_menu->addMenu(tr("Arran&ge"));
  layer_arrange_menu->setObjectName(QStringLiteral("layerArrangeMenu"));
  auto* layer_up_action = layer_arrange_menu->addAction(tr("Move Layer &Up"));
  auto* layer_down_action = layer_arrange_menu->addAction(tr("Move Layer &Down"));
  layer_arrange_menu->addSeparator();
  auto* flip_h_action = layer_arrange_menu->addAction(tr("Flip Layer &Horizontal"));
  auto* flip_v_action = layer_arrange_menu->addAction(tr("Flip Layer &Vertical"));
  // Align / Distribute nest under Arrange: the Layer menu sits at its 23-row
  // cap (ui_main_window_renders_color_controls). The Move tool's options-bar
  // buttons wrap these same QActions (docs/alignment.md).
  layer_arrange_menu->addSeparator();
  auto* layer_align_menu = layer_arrange_menu->addMenu(tr("&Align"));
  layer_align_menu->setObjectName(QStringLiteral("layerAlignMenu"));
  auto* layer_distribute_menu = layer_arrange_menu->addMenu(tr("&Distribute"));
  layer_distribute_menu->setObjectName(QStringLiteral("layerDistributeMenu"));
  {
    struct AlignSpec {
      AlignEdge edge;
      const char* text;
      const char* object_name;
      const char* command_id;
    };
    const AlignSpec align_specs[] = {
        {AlignEdge::Left, QT_TR_NOOP("Align &Left Edges"), "layerAlignLeftAction", "layer.align_left"},
        {AlignEdge::HorizontalCenter, QT_TR_NOOP("Align &Horizontal Centers"), "layerAlignHCenterAction",
         "layer.align_horizontal_centers"},
        {AlignEdge::Right, QT_TR_NOOP("Align &Right Edges"), "layerAlignRightAction", "layer.align_right"},
        {AlignEdge::Top, QT_TR_NOOP("Align &Top Edges"), "layerAlignTopAction", "layer.align_top"},
        {AlignEdge::VerticalCenter, QT_TR_NOOP("Align &Vertical Centers"), "layerAlignVCenterAction",
         "layer.align_vertical_centers"},
        {AlignEdge::Bottom, QT_TR_NOOP("Align &Bottom Edges"), "layerAlignBottomAction", "layer.align_bottom"},
    };
    for (const auto& spec : align_specs) {
      auto* action = layer_align_menu->addAction(tr(spec.text));
      bind_action_text(action, spec.text);
      action->setObjectName(QLatin1String(spec.object_name));
      action->setIcon(align_edge_icon(spec.edge));
      register_hotkey(action, spec.command_id);
      connect(action, &QAction::triggered, this, [this, edge = spec.edge] { align_selected_layers(edge); });
      register_document_action(action);
      layer_align_actions_[static_cast<std::size_t>(spec.edge)] = action;
    }
    layer_align_menu->addSeparator();
    auto* align_to_group = new QActionGroup(this);
    align_to_group->setExclusive(true);
    layer_align_to_selection_action_ = layer_align_menu->addAction(tr("Align To: &Selection"));
    bind_action_text(layer_align_to_selection_action_, QT_TR_NOOP("Align To: &Selection"));
    layer_align_to_selection_action_->setObjectName(QStringLiteral("layerAlignToSelectionAction"));
    layer_align_to_selection_action_->setCheckable(true);
    layer_align_to_selection_action_->setChecked(true);
    align_to_group->addAction(layer_align_to_selection_action_);
    register_hotkey(layer_align_to_selection_action_, "layer.align_to_selection");
    layer_align_to_canvas_action_ = layer_align_menu->addAction(tr("Align To: &Canvas"));
    bind_action_text(layer_align_to_canvas_action_, QT_TR_NOOP("Align To: &Canvas"));
    layer_align_to_canvas_action_->setObjectName(QStringLiteral("layerAlignToCanvasAction"));
    layer_align_to_canvas_action_->setCheckable(true);
    align_to_group->addAction(layer_align_to_canvas_action_);
    register_hotkey(layer_align_to_canvas_action_, "layer.align_to_canvas");
    connect(layer_align_to_canvas_action_, &QAction::toggled, this,
            [this](bool checked) { set_align_to_canvas(checked); });
    register_document_action(layer_align_to_selection_action_);
    register_document_action(layer_align_to_canvas_action_);

    struct DistributeSpec {
      DistributeMode mode;
      const char* text;
      const char* object_name;
      const char* command_id;
    };
    const DistributeSpec distribute_specs[] = {
        {DistributeMode::Left, QT_TR_NOOP("Distribute &Left Edges"), "layerDistributeLeftAction",
         "layer.distribute_left"},
        {DistributeMode::HorizontalCenter, QT_TR_NOOP("Distribute &Horizontal Centers"),
         "layerDistributeHCenterAction", "layer.distribute_horizontal_centers"},
        {DistributeMode::Right, QT_TR_NOOP("Distribute &Right Edges"), "layerDistributeRightAction",
         "layer.distribute_right"},
        {DistributeMode::Top, QT_TR_NOOP("Distribute &Top Edges"), "layerDistributeTopAction",
         "layer.distribute_top"},
        {DistributeMode::VerticalCenter, QT_TR_NOOP("Distribute &Vertical Centers"),
         "layerDistributeVCenterAction", "layer.distribute_vertical_centers"},
        {DistributeMode::Bottom, QT_TR_NOOP("Distribute &Bottom Edges"), "layerDistributeBottomAction",
         "layer.distribute_bottom"},
        {DistributeMode::HorizontalSpacing, QT_TR_NOOP("Distribute Horizontal &Spacing"),
         "layerDistributeHSpacingAction", "layer.distribute_horizontal_spacing"},
        {DistributeMode::VerticalSpacing, QT_TR_NOOP("Distribute Vertical S&pacing"),
         "layerDistributeVSpacingAction", "layer.distribute_vertical_spacing"},
    };
    for (const auto& spec : distribute_specs) {
      if (spec.mode == DistributeMode::HorizontalSpacing) {
        layer_distribute_menu->addSeparator();
      }
      auto* action = layer_distribute_menu->addAction(tr(spec.text));
      bind_action_text(action, spec.text);
      action->setObjectName(QLatin1String(spec.object_name));
      register_hotkey(action, spec.command_id);
      connect(action, &QAction::triggered, this, [this, mode = spec.mode] { distribute_selected_layers(mode); });
      register_document_action(action);
      layer_distribute_actions_[static_cast<std::size_t>(spec.mode)] = action;
    }
  }
  add_layer_action->setObjectName(QStringLiteral("layerNewAction"));
  add_folder_action->setObjectName(QStringLiteral("layerNewFolderAction"));
  layer_via_copy_action->setObjectName(QStringLiteral("layerViaCopyAction"));
  layer_via_cut_action->setObjectName(QStringLiteral("layerViaCutAction"));
  add_mask_action->setObjectName(QStringLiteral("layerAddMaskAction"));
  edit_layer_mask_action_->setObjectName(QStringLiteral("layerEditMaskAction"));
  mask_overlay_action_->setObjectName(QStringLiteral("layerMaskOverlayAction"));
  view_layer_mask_action_->setObjectName(QStringLiteral("layerViewMaskAction"));
  delete_layer_mask_action_->setObjectName(QStringLiteral("layerDeleteMaskAction"));
  link_layer_mask_action_->setObjectName(QStringLiteral("layerLinkMaskAction"));
  disable_layer_mask_action_->setObjectName(QStringLiteral("layerDisableMaskAction"));
  invert_layer_mask_action_->setObjectName(QStringLiteral("layerInvertMaskAction"));
  apply_layer_mask_action_->setObjectName(QStringLiteral("layerApplyMaskAction"));
  layer_clipping_mask_action_->setObjectName(QStringLiteral("layerClippingMaskAction"));
  edit_adjustment_action->setObjectName(QStringLiteral("layerEditAdjustmentAction"));
  layer_blending_options_action_->setObjectName(QStringLiteral("layerBlendingOptionsAction"));
  layer_copy_style_action_->setObjectName(QStringLiteral("layerCopyStyleAction"));
  layer_paste_style_action_->setObjectName(QStringLiteral("layerPasteStyleAction"));
  layer_delete_style_action_->setObjectName(QStringLiteral("layerDeleteStyleAction"));
  layer_rasterize_action_->setObjectName(QStringLiteral("layerRasterizeAction"));
  layer_rasterize_layer_style_action_->setObjectName(QStringLiteral("layerRasterizeLayerStyleAction"));
  layer_convert_smart_object_action_->setObjectName(QStringLiteral("layerConvertSmartObjectAction"));
  layer_smart_object_edit_action_->setObjectName(QStringLiteral("layerSmartObjectEditAction"));
  layer_smart_object_replace_action_->setObjectName(QStringLiteral("layerSmartObjectReplaceAction"));
  layer_smart_object_export_action_->setObjectName(QStringLiteral("layerSmartObjectExportAction"));
  layer_smart_object_via_copy_action_->setObjectName(QStringLiteral("layerSmartObjectViaCopyAction"));
  layer_smart_object_update_action_->setObjectName(QStringLiteral("layerSmartObjectUpdateAction"));
  layer_smart_object_relink_action_->setObjectName(QStringLiteral("layerSmartObjectRelinkAction"));
  layer_smart_object_embed_action_->setObjectName(QStringLiteral("layerSmartObjectEmbedAction"));
  layer_smart_object_to_normal_action_->setObjectName(QStringLiteral("layerSmartObjectToNormalAction"));
  duplicate_layer_action->setObjectName(QStringLiteral("layerDuplicateAction"));
  delete_layer_action->setObjectName(QStringLiteral("layerDeleteAction"));
  fill_layer_action->setObjectName(QStringLiteral("layerFillForegroundAction"));
  fill_background_action->setObjectName(QStringLiteral("layerFillBackgroundAction"));
  clear_layer_action->setObjectName(QStringLiteral("layerClearAction"));
  add_layer_action->setIcon(simple_icon(QStringLiteral("new")));
  add_folder_action->setIcon(simple_icon(QStringLiteral("dir"), QColor(245, 205, 105)));
  layer_via_copy_action->setIcon(simple_icon(QStringLiteral("copy")));
  layer_via_cut_action->setIcon(simple_icon(QStringLiteral("cut"), QColor(255, 185, 120)));
  add_mask_action->setIcon(simple_icon(QStringLiteral("mask"), QColor(210, 220, 230)));
  edit_layer_mask_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(150, 205, 255)));
  mask_overlay_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(255, 120, 120)));
  view_layer_mask_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(235, 235, 235)));
  delete_layer_mask_action_->setIcon(simple_icon(QStringLiteral("mask"), QColor(255, 150, 150)));
  link_layer_mask_action_->setIcon(simple_icon(QStringLiteral("link"), QColor(210, 220, 230)));
  disable_layer_mask_action_->setIcon(simple_icon(QStringLiteral("off"), QColor(255, 190, 120)));
  invert_layer_mask_action_->setIcon(simple_icon(QStringLiteral("inv"), QColor(210, 220, 230)));
  apply_layer_mask_action_->setIcon(simple_icon(QStringLiteral("ok"), QColor(160, 220, 165)));
  layer_clipping_mask_action_->setIcon(simple_icon(QStringLiteral("clip"), QColor(150, 205, 255)));
  link_layer_mask_action_->setCheckable(true);
  disable_layer_mask_action_->setCheckable(true);
  edit_layer_mask_action_->setCheckable(true);
  mask_overlay_action_->setCheckable(true);
  view_layer_mask_action_->setCheckable(true);
  edit_adjustment_action->setIcon(simple_icon(QStringLiteral("ADJ"), QColor(190, 220, 255)));
  layer_blending_options_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_copy_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_paste_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_delete_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(255, 150, 150)));
  layer_rasterize_action_->setIcon(simple_icon(QStringLiteral("RA"), QColor(220, 220, 160)));
  layer_rasterize_layer_style_action_->setIcon(simple_icon(QStringLiteral("fx"), QColor(170, 210, 255)));
  layer_smart_object_to_normal_action_->setIcon(simple_icon(QStringLiteral("RA"), QColor(220, 220, 160)));
  duplicate_layer_action->setIcon(simple_icon(QStringLiteral("dup")));
  merge_visible_action->setIcon(simple_icon(QStringLiteral("merge")));
  merge_down_action->setIcon(simple_icon(QStringLiteral("merge"), QColor(160, 220, 255)));
  rename_layer_action->setIcon(simple_icon(QStringLiteral("RN")));
  delete_layer_action->setIcon(simple_icon(QStringLiteral("trash")));
  fill_layer_action->setIcon(simple_icon(QStringLiteral("fill")));
  fill_background_action->setIcon(simple_icon(QStringLiteral("fill"), QColor(160, 190, 255)));
  clear_layer_action->setIcon(simple_icon(QStringLiteral("clear")));
  flip_h_action->setIcon(simple_icon(QStringLiteral("FH")));
  flip_v_action->setIcon(simple_icon(QStringLiteral("FV")));
  layer_up_action->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
  layer_down_action->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
  register_hotkey(add_layer_action, "layer.new", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  register_hotkey(layer_via_copy_action, "layer.via_copy", QKeySequence(Qt::CTRL | Qt::Key_J));
  register_hotkey(layer_via_cut_action, "layer.via_cut", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
  register_hotkey(merge_visible_action, "layer.merge_visible", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  register_hotkey(merge_down_action, "layer.merge_down", QKeySequence(Qt::CTRL | Qt::Key_E));
  register_hotkey(edit_layer_mask_action_, "layer.edit_mask", QKeySequence(Qt::CTRL | Qt::Key_Backslash));
  register_hotkey(mask_overlay_action_, "layer.mask_overlay", QKeySequence(Qt::Key_Backslash));
  register_hotkey(view_layer_mask_action_, "layer.view_mask");
  register_hotkey(fill_layer_action, "layer.fill", QKeySequence(Qt::ALT | Qt::Key_Backspace));
  register_hotkey(fill_background_action, "layer.fill_background", QKeySequence(Qt::CTRL | Qt::Key_Backspace));
  // Mac keyboards' delete key sends Backspace (Photoshop accepts both there); the
  // forward-delete key stays bound too. Windows/Linux keep plain Delete.
  register_hotkey(clear_layer_action, "layer.clear",
                  platform_hotkeys({QKeySequence(Qt::Key_Delete)},
                                   {QKeySequence(Qt::Key_Backspace), QKeySequence(Qt::Key_Delete)}));
  register_hotkey(add_folder_action, "layer.new_folder", QKeySequence(Qt::CTRL | Qt::Key_G));
  register_hotkey(add_mask_action, "layer.add_mask");
  register_hotkey(delete_layer_mask_action_, "layer.delete_mask");
  register_hotkey(invert_layer_mask_action_, "layer.invert_mask");
  register_hotkey(apply_layer_mask_action_, "layer.apply_mask");
  register_hotkey(layer_clipping_mask_action_, "layer.toggle_clipping_mask",
                  QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G));
  register_hotkey(edit_adjustment_action, "layer.edit_adjustment");
  register_hotkey(layer_blending_options_action_, "layer.styles");
  register_hotkey(duplicate_layer_action, "layer.duplicate");
  // F2: the Explorer, Blender and VS Code rename key on every platform.
  register_hotkey(rename_layer_action, "layer.rename", QKeySequence(Qt::Key_F2));
  register_hotkey(delete_layer_action, "layer.delete");
  register_hotkey(flip_h_action, "layer.flip_horizontal");
  register_hotkey(flip_v_action, "layer.flip_vertical");
  register_hotkey(layer_up_action, "layer.move_up");
  register_hotkey(layer_down_action, "layer.move_down");
  connect(add_layer_action, &QAction::triggered, this, [this] { add_layer(); });
  connect(add_folder_action, &QAction::triggered, this, [this] { create_layer_folder(); });
  connect(layer_via_copy_action, &QAction::triggered, this, [this] { layer_via_copy(); });
  connect(layer_via_cut_action, &QAction::triggered, this, [this] { layer_via_cut(); });
  connect(add_mask_action, &QAction::triggered, this, [this] { add_layer_mask(); });
  connect(edit_layer_mask_action_, &QAction::triggered, this, [this](bool checked) {
    if (!checked) {
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, true);
      return;
    }
    const auto active = std::as_const(document()).active_layer_id();
    const auto* layer = active.has_value()
                            ? std::as_const(document()).find_layer(*active)
                            : nullptr;
    if (layer != nullptr && layer->mask().has_value()) {
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Mask, true);
    } else if (active.has_value()) {
      static_cast<void>(set_smart_filter_mask_edit_target_ui(
          *active, CanvasWidget::MaskDisplayMode::Overlay, true));
    }
  });
  connect(mask_overlay_action_, &QAction::triggered, this, [this](bool checked) { set_mask_overlay_shown(checked); });
  connect(view_layer_mask_action_, &QAction::triggered, this,
          [this](bool checked) { set_layer_mask_view_shown(checked); });
  connect(delete_layer_mask_action_, &QAction::triggered, this, [this] { delete_active_layer_mask(); });
  connect(link_layer_mask_action_, &QAction::triggered, this,
          [this](bool checked) { set_active_layer_mask_linked(checked); });
  connect(disable_layer_mask_action_, &QAction::triggered, this,
          [this](bool checked) { set_active_layer_mask_disabled(checked); });
  connect(invert_layer_mask_action_, &QAction::triggered, this, [this] { invert_active_layer_mask(); });
  connect(apply_layer_mask_action_, &QAction::triggered, this, [this] { apply_active_layer_mask(); });
  connect(layer_clipping_mask_action_, &QAction::triggered, this, [this] { toggle_active_layer_clipping(); });
  connect(edit_adjustment_action, &QAction::triggered, this, [this] { edit_active_adjustment_layer(); });
  connect(layer_blending_options_action_, &QAction::triggered, this, [this] { edit_active_layer_style(); });
  connect(layer_copy_style_action_, &QAction::triggered, this, [this] { copy_active_layer_style(); });
  connect(layer_paste_style_action_, &QAction::triggered, this, [this] { paste_layer_style_to_selected_layers(); });
  connect(layer_delete_style_action_, &QAction::triggered, this, [this] { delete_selected_layer_styles(); });
  connect(layer_rasterize_action_, &QAction::triggered, this, [this] { rasterize_active_layers(); });
  connect(layer_rasterize_layer_style_action_, &QAction::triggered, this,
          [this] { rasterize_active_layer_styles(); });
  connect(layer_convert_smart_object_action_, &QAction::triggered, this, [this] { convert_to_smart_object(); });
  connect(layer_smart_object_edit_action_, &QAction::triggered, this, [this] { open_smart_object_contents(); });
  connect(layer_smart_object_replace_action_, &QAction::triggered, this,
          [this] { replace_smart_object_contents(); });
  connect(layer_smart_object_export_action_, &QAction::triggered, this, [this] { export_smart_object_contents(); });
  connect(layer_smart_object_via_copy_action_, &QAction::triggered, this, [this] { new_smart_object_via_copy(); });
  connect(layer_smart_object_to_normal_action_, &QAction::triggered, this, [this] { rasterize_active_layers(); });
  connect(layer_smart_object_update_action_, &QAction::triggered, this, [this] { update_smart_object_content(); });
  connect(layer_smart_object_relink_action_, &QAction::triggered, this,
          [this] { relink_smart_object_contents(); });
  connect(layer_smart_object_embed_action_, &QAction::triggered, this, [this] { embed_linked_smart_object(); });
  connect(duplicate_layer_action, &QAction::triggered, this, [this] { duplicate_active_layer(); });
  // Photoshop's Duplicate Layer dialog with a destination document. It lives
  // in the layer context menu (the Layer menu's row count is pinned) and on
  // the window itself, so its hotkey works without a menu-bar row.
  duplicate_layer_to_document_action_ = new QAction(tr("Duplicate Layer to Document..."), this);
  duplicate_layer_to_document_action_->setObjectName(QStringLiteral("layerDuplicateToDocumentAction"));
  duplicate_layer_to_document_action_->setIcon(simple_icon(QStringLiteral("dup")));
  register_hotkey(duplicate_layer_to_document_action_, "layer.duplicate_to_document");
  connect(duplicate_layer_to_document_action_, &QAction::triggered, this,
          [this] { duplicate_layer_to_document(); });
  addAction(duplicate_layer_to_document_action_);
  register_document_action(duplicate_layer_to_document_action_);
  connect(merge_visible_action, &QAction::triggered, this, [this] { merge_visible_to_new_layer(); });
  connect(merge_down_action, &QAction::triggered, this, [this] { merge_down(); });
  connect(rename_layer_action, &QAction::triggered, this, [this] { rename_active_layer(); });
  connect(delete_layer_action, &QAction::triggered, this, [this] { delete_active_layer(); });
  connect(fill_layer_action, &QAction::triggered, this, [this] { fill_active_layer(); });
  connect(fill_background_action, &QAction::triggered, this, [this] {
    fill_active_layer_with_color(canvas_->secondary_color(), tr("Fill background"));
  });
  connect(clear_layer_action, &QAction::triggered, this, [this] { clear_active_layer(); });
  connect(flip_h_action, &QAction::triggered, this, [this] { flip_active_layer_horizontal(); });
  connect(flip_v_action, &QAction::triggered, this, [this] { flip_active_layer_vertical(); });
  connect(layer_up_action, &QAction::triggered, this, [this] { move_active_layer(1); });
  connect(layer_down_action, &QAction::triggered, this, [this] { move_active_layer(-1); });
  for (auto* action : {add_layer_action, add_folder_action, layer_new_menu->menuAction(),
                       new_adjustment_layer_menu->menuAction(), new_fill_layer_menu->menuAction(),
                       layer_mask_menu->menuAction(), vector_mask_menu->menuAction(),
                       layer_arrange_menu->menuAction(),
                       layer_via_copy_action, layer_via_cut_action, add_mask_action, layer_clipping_mask_action_,
                       duplicate_layer_action, merge_visible_action, merge_down_action, rename_layer_action,
                       delete_layer_action, fill_layer_action, fill_background_action, clear_layer_action,
                       flip_h_action, flip_v_action, layer_up_action, layer_down_action}) {
    register_document_action(action);
  }

  auto* image_mode_menu = image_menu->addMenu(tr("&Mode"));
  image_mode_menu->setObjectName(QStringLiteral("imageModeMenu"));
  bind_action_text(image_mode_menu->menuAction(), QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Mode"));
  image_mode_rgb_action_ = image_mode_menu->addAction(tr("&RGB Color"));
  bind_action_text(image_mode_rgb_action_, QT_TR_NOOP("&RGB Color"));
  image_mode_rgb_action_->setObjectName(QStringLiteral("imageModeRgbAction"));
  image_mode_rgb_action_->setCheckable(true);
  image_mode_rgb_action_->setChecked(true);
  register_hotkey(image_mode_rgb_action_, "image.mode_rgb");
  connect(image_mode_rgb_action_, &QAction::triggered, this, [this] { convert_document_to_rgb(); });
  image_mode_indexed_action_ = image_mode_menu->addAction(tr("&Indexed (Palette)..."));
  bind_action_text(image_mode_indexed_action_, QT_TR_NOOP("&Indexed (Palette)..."));
  image_mode_indexed_action_->setObjectName(QStringLiteral("imageModeIndexedAction"));
  image_mode_indexed_action_->setCheckable(true);
  register_hotkey(image_mode_indexed_action_, "image.mode_indexed");
  connect(image_mode_indexed_action_, &QAction::triggered, this, [this] { convert_document_to_indexed(); });
  auto* image_mode_group = new QActionGroup(this);
  image_mode_group->setExclusive(true);
  image_mode_group->addAction(image_mode_rgb_action_);
  image_mode_group->addAction(image_mode_indexed_action_);
  snap_layer_to_palette_action_ = image_menu->addAction(tr("Snap &Layer to Palette"));
  bind_action_text(snap_layer_to_palette_action_, QT_TR_NOOP("Snap &Layer to Palette"));
  snap_layer_to_palette_action_->setObjectName(QStringLiteral("imageSnapLayerToPaletteAction"));
  register_hotkey(snap_layer_to_palette_action_, "image.snap_layer_to_palette");
  connect(snap_layer_to_palette_action_, &QAction::triggered, this, [this] { snap_layers_to_palette(true); });
  snap_image_to_palette_action_ = image_menu->addAction(tr("Snap Image to &Palette"));
  bind_action_text(snap_image_to_palette_action_, QT_TR_NOOP("Snap Image to &Palette"));
  snap_image_to_palette_action_->setObjectName(QStringLiteral("imageSnapImageToPaletteAction"));
  register_hotkey(snap_image_to_palette_action_, "image.snap_image_to_palette");
  connect(snap_image_to_palette_action_, &QAction::triggered, this, [this] { snap_layers_to_palette(false); });
  for (auto* action : {image_mode_menu->menuAction(), image_mode_rgb_action_, image_mode_indexed_action_,
                       snap_layer_to_palette_action_, snap_image_to_palette_action_}) {
    register_document_action(action);
  }
  image_menu->addSeparator();

  auto* adjustments_menu = image_menu->addMenu(tr("&Adjustments"));
  adjustments_menu->setObjectName(QStringLiteral("imageAdjustmentsMenu"));
  const auto add_adjustment_action = [this, adjustments_menu](const char* source, const QString& object_name,
                                                              const QString& identifier,
                                                              const QKeySequence& shortcut = {}) {
    auto* action = adjustments_menu->addAction(tr(source));
    bind_action_text(action, source);
    const auto label = action->text();
    action->setObjectName(object_name);
    action->setIcon(simple_icon(label.left(3).toUpper()));
    register_hotkey(action, identifier, shortcut);
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
    register_document_action(action);
    return action;
  };
  add_adjustment_action(QT_TR_NOOP("&Invert"), QStringLiteral("imageAdjustInvertAction"),
                        QStringLiteral("patchy.filters.invert"), QKeySequence(Qt::CTRL | Qt::Key_I));
  auto* levels_action = adjustments_menu->addAction(tr("&Levels..."));
  levels_action->setObjectName(QStringLiteral("imageAdjustLevelsAction"));
  levels_action->setIcon(simple_icon(QStringLiteral("LVL")));
  register_hotkey(levels_action, "image.levels", QKeySequence(Qt::CTRL | Qt::Key_L));
  connect(levels_action, &QAction::triggered, this, [this] { levels_dialog(); });
  register_document_action(levels_action);
  auto* curves_action = adjustments_menu->addAction(tr("&Curves..."));
  curves_action->setObjectName(QStringLiteral("imageAdjustCurvesAction"));
  curves_action->setIcon(simple_icon(QStringLiteral("CRV")));
  register_hotkey(curves_action, "image.curves", QKeySequence(Qt::CTRL | Qt::Key_M));
  connect(curves_action, &QAction::triggered, this, [this] { curves_dialog(); });
  register_document_action(curves_action);
  auto* hue_saturation_action = adjustments_menu->addAction(tr("&Hue/Saturation..."));
  hue_saturation_action->setObjectName(QStringLiteral("imageAdjustHueSaturationAction"));
  hue_saturation_action->setIcon(simple_icon(QStringLiteral("HSL")));
  register_hotkey(hue_saturation_action, "image.hue_saturation", QKeySequence(Qt::CTRL | Qt::Key_U));
  connect(hue_saturation_action, &QAction::triggered, this, [this] { hue_saturation_dialog(); });
  register_document_action(hue_saturation_action);
  auto* color_balance_action = adjustments_menu->addAction(tr("Color &Balance..."));
  color_balance_action->setObjectName(QStringLiteral("imageAdjustColorBalanceAction"));
  color_balance_action->setIcon(simple_icon(QStringLiteral("CB")));
  register_hotkey(color_balance_action, "image.color_balance", QKeySequence(Qt::CTRL | Qt::Key_B));
  connect(color_balance_action, &QAction::triggered, this, [this] { color_balance_dialog(); });
  register_document_action(color_balance_action);
  add_adjustment_action(QT_TR_NOOP("&Desaturate"), QStringLiteral("imageAdjustDesaturateAction"),
                        QStringLiteral("patchy.filters.desaturate"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  add_adjustment_action(QT_TR_NOOP("&Auto Tone"), QStringLiteral("imageAdjustAutoToneAction"),
                        QStringLiteral("patchy.filters.auto_tone"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
  add_adjustment_action(QT_TR_NOOP("Auto &Contrast"), QStringLiteral("imageAdjustAutoContrastAction"),
                        QStringLiteral("patchy.filters.auto_contrast"),
                        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
  add_adjustment_action(QT_TR_NOOP("Auto Colo&r"), QStringLiteral("imageAdjustAutoColorAction"),
                        QStringLiteral("patchy.filters.auto_color"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
  auto* auto_all_action = adjustments_menu->addAction(tr("A&uto All"));
  bind_action_text(auto_all_action, QT_TR_NOOP("A&uto All"));
  auto_all_action->setObjectName(QStringLiteral("imageAdjustAutoAllAction"));
  auto_all_action->setIcon(simple_icon(QStringLiteral("AA")));
  register_hotkey(auto_all_action, "image.auto_all");
  connect(auto_all_action, &QAction::triggered, this, [this] { auto_all_adjustments(); });
  register_document_action(auto_all_action);
  adjustments_menu->addSeparator();
  add_adjustment_action(QT_TR_NOOP("&Brightness/Contrast..."), QStringLiteral("imageAdjustBrightnessContrastAction"),
                        QStringLiteral("patchy.filters.brightness_contrast"));
  add_adjustment_action(QT_TR_NOOP("&Threshold"), QStringLiteral("imageAdjustThresholdAction"),
                        QStringLiteral("patchy.filters.threshold"));
  add_adjustment_action(QT_TR_NOOP("&Posterize"), QStringLiteral("imageAdjustPosterizeAction"),
                        QStringLiteral("patchy.filters.posterize"));
  image_menu->addSeparator();

  auto* image_size_action = image_menu->addAction(tr("&Image Size..."));
  image_size_action->setObjectName(QStringLiteral("imageSizeAction"));
  auto* canvas_size_action = image_menu->addAction(tr("&Canvas Size..."));
  canvas_size_action->setObjectName(QStringLiteral("imageCanvasSizeAction"));
  auto* crop_action = image_menu->addAction(tr("&Crop to Selection"));
  crop_action->setObjectName(QStringLiteral("imageCropToSelectionAction"));
  image_menu->addSeparator();
  // "Right" is the 90-degree clockwise turn and "Left" the counterclockwise one; the object
  // names and hotkey ids keep their persisted clockwise/counterclockwise identities.
  auto* rotate_cw_action = image_menu->addAction(tr("Rotate &Right"));
  auto* rotate_ccw_action = image_menu->addAction(tr("Rotate &Left"));
  auto* rotate_arbitrary_action = image_menu->addAction(tr("Rotate &Arbitrary..."));
  rotate_cw_action->setObjectName(QStringLiteral("imageRotateClockwiseAction"));
  rotate_ccw_action->setObjectName(QStringLiteral("imageRotateCounterclockwiseAction"));
  rotate_arbitrary_action->setObjectName(QStringLiteral("imageRotateArbitraryAction"));
  bind_translated_status_tip(rotate_cw_action, QT_TR_NOOP("Rotate the canvas 90 degrees clockwise"));
  apply_bound_translation(rotate_cw_action);
  bind_translated_status_tip(rotate_ccw_action, QT_TR_NOOP("Rotate the canvas 90 degrees counterclockwise"));
  apply_bound_translation(rotate_ccw_action);
  bind_translated_status_tip(rotate_arbitrary_action, QT_TR_NOOP("Rotate the canvas by any angle, enlarging it to fit"));
  apply_bound_translation(rotate_arbitrary_action);
  image_size_action->setIcon(simple_icon(QStringLiteral("IS")));
  canvas_size_action->setIcon(simple_icon(QStringLiteral("CS")));
  crop_action->setIcon(simple_icon(QStringLiteral("crop")));
  rotate_cw_action->setIcon(simple_icon(QStringLiteral("rotate")));
  rotate_ccw_action->setIcon(simple_icon(QStringLiteral("rotate")));
  rotate_arbitrary_action->setIcon(simple_icon(QStringLiteral("rotate")));
  register_hotkey(image_size_action, "image.size", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I));
  register_hotkey(canvas_size_action, "image.canvas_size", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
  // Plain C now belongs to the Crop tool (tools.crop); the menu command keeps
  // its persisted id but ships without a default, like Photoshop's Image menu.
  register_hotkey(crop_action, "image.crop_to_selection");
  register_hotkey(rotate_cw_action, "image.rotate_cw", QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
  register_hotkey(rotate_ccw_action, "image.rotate_ccw", QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));
  register_hotkey(rotate_arbitrary_action, "image.rotate_arbitrary");
  auto* shift_seams_action = image_menu->addAction(tr("Shift &Seams to Center"));
  shift_seams_action->setObjectName(QStringLiteral("imageShiftSeamsAction"));
  shift_seams_action->setStatusTip(tr("Wrap the image by half its size so tiling seams land in the middle; press again to shift back"));
  bind_translated_status_tip(
      shift_seams_action,
      "Wrap the image by half its size so tiling seams land in the middle; press again to shift back");
  register_hotkey(shift_seams_action, "image.shift_seams");
  connect(image_size_action, &QAction::triggered, this, [this] { resize_image_dialog(); });
  connect(canvas_size_action, &QAction::triggered, this, [this] { resize_canvas_dialog(); });
  connect(crop_action, &QAction::triggered, this, [this] { crop_to_selection(); });
  connect(rotate_cw_action, &QAction::triggered, this, [this] { rotate_canvas_clockwise(); });
  connect(rotate_ccw_action, &QAction::triggered, this, [this] { rotate_canvas_counterclockwise(); });
  connect(rotate_arbitrary_action, &QAction::triggered, this, [this] { rotate_canvas_arbitrary(); });
  connect(shift_seams_action, &QAction::triggered, this, [this] { toggle_tile_seam_offset(); });
  auto* divide_photos_action = image_menu->addAction(tr("Divide Scanned P&hotos..."));
  bind_action_text(divide_photos_action, QT_TR_NOOP("Divide Scanned P&hotos..."));
  divide_photos_action->setObjectName(QStringLiteral("imageDivideScannedPhotosAction"));
  divide_photos_action->setStatusTip(tr("Detect the photos in this image and open or save each one as its own image"));
  bind_translated_status_tip(
      divide_photos_action,
      "Detect the photos in this image and open or save each one as its own image");
  register_hotkey(divide_photos_action, "image.divide_photos");
  connect(divide_photos_action, &QAction::triggered, this, [this] { divide_current_document_photos(); });
  for (auto* action : {adjustments_menu->menuAction(), image_size_action, canvas_size_action, crop_action,
                       rotate_cw_action, rotate_ccw_action, rotate_arbitrary_action, shift_seams_action,
                       divide_photos_action}) {
    register_document_action(action);
  }

  filter_convert_smart_filters_action_ =
      filter_menu->addAction(tr("Convert for Smart Filters"));
  filter_convert_smart_filters_action_->setObjectName(
      QStringLiteral("filterConvertForSmartFiltersAction"));
  filter_convert_smart_filters_action_->setProperty(
      "patchy.channelViewBlocked", true);
  filter_convert_smart_filters_action_->setIcon(
      simple_icon(QStringLiteral("SO")));
  filter_convert_smart_filters_action_->setStatusTip(tr("Convert the active layer to a Smart Object for editable filters"));
  bind_action_text(filter_convert_smart_filters_action_,
                   QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Convert for Smart Filters"));
  bind_translated_status_tip(
      filter_convert_smart_filters_action_,
      "Convert the active layer to a Smart Object for editable filters");
  apply_bound_translation(filter_convert_smart_filters_action_);
  refresh_action_tooltip(filter_convert_smart_filters_action_);
  register_hotkey(filter_convert_smart_filters_action_,
                  "filter.convert_for_smart_filters");
  connect(filter_convert_smart_filters_action_, &QAction::triggered, this,
          [this] { convert_for_smart_filters(); });
  register_document_action(filter_convert_smart_filters_action_);
  filter_menu->addSeparator();

  auto* filter_gallery_action = filter_menu->addAction(tr("Filter &Gallery..."));
  filter_gallery_action->setObjectName(QStringLiteral("filterGalleryAction"));
  filter_gallery_action->setProperty("patchy.channelViewBlocked", true);
  filter_gallery_action->setIcon(simple_icon(QStringLiteral("FX")));
  filter_gallery_action->setStatusTip(tr("Preview and apply visual filters and photo looks"));
  bind_action_text(filter_gallery_action, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Filter &Gallery..."));
  bind_translated_status_tip(filter_gallery_action, "Preview and apply visual filters and photo looks");
  apply_bound_translation(filter_gallery_action);
  refresh_action_tooltip(filter_gallery_action);
  register_hotkey(filter_gallery_action, "filter.gallery");
  connect(filter_gallery_action, &QAction::triggered, this, [this] { visual_filter_gallery_dialog(); });
  register_document_action(filter_gallery_action);

  auto* liquify_action = filter_menu->addAction(tr("&Liquify..."));
  liquify_action->setObjectName(QStringLiteral("filterLiquifyAction"));
  liquify_action->setProperty("patchy.channelViewBlocked", true);
  liquify_action->setIcon(simple_icon(QStringLiteral("LIQ")));
  liquify_action->setStatusTip(tr("Push, pull, twist, pucker, or bloat pixels with a brush"));
  bind_action_text(liquify_action, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Liquify..."));
  bind_translated_status_tip(
      liquify_action,
      "Push, pull, twist, pucker, or bloat pixels with a brush");
  apply_bound_translation(liquify_action);
  refresh_action_tooltip(liquify_action);
  register_hotkey(liquify_action, "filter.liquify",
                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X));
  connect(liquify_action, &QAction::triggered, this,
          [this] { liquify_dialog(); });
  register_document_action(liquify_action);
  filter_menu->addSeparator();

  const auto add_filter_submenu = [this, filter_menu](
                                      const char* object_name,
                                      FilterCategory category) {
    auto* menu = filter_menu->addMenu(filter_category_display_name(category));
    menu->setObjectName(QString::fromLatin1(object_name));
    QPointer<QAction> menu_action(menu->menuAction());
    register_retranslation([menu_action, category] {
      if (menu_action != nullptr) {
        menu_action->setText(filter_category_display_name(category));
      }
    });
    register_document_action(menu->menuAction());
    return menu;
  };
  auto* filter_photo_looks_menu = add_filter_submenu(
      "filterPhotoLooksMenu", FilterCategory::PhotoLooks);
  auto* filter_blur_menu =
      add_filter_submenu("filterBlurMenu", FilterCategory::Blur);
  auto* filter_sharpen_menu =
      add_filter_submenu("filterSharpenMenu", FilterCategory::Sharpen);
  auto* filter_distort_menu =
      add_filter_submenu("filterDistortMenu", FilterCategory::Distort);
  auto* filter_noise_menu =
      add_filter_submenu("filterNoiseMenu", FilterCategory::Noise);
  auto* filter_pixelate_menu =
      add_filter_submenu("filterPixelateMenu", FilterCategory::Pixelate);
  auto* filter_artistic_menu =
      add_filter_submenu("filterArtisticMenu", FilterCategory::Artistic);
  auto* filter_stylize_menu =
      add_filter_submenu("filterStylizeMenu", FilterCategory::Stylize);
  auto* filter_render_menu =
      add_filter_submenu("filterRenderMenu", FilterCategory::Render);
  const auto menu_for_filter = [filter_menu, filter_photo_looks_menu, filter_blur_menu, filter_sharpen_menu,
                                filter_distort_menu, filter_noise_menu, filter_pixelate_menu, filter_stylize_menu,
                                filter_render_menu, filter_artistic_menu](FilterCategory category) {
    switch (category) {
      case FilterCategory::PhotoLooks:
        return filter_photo_looks_menu;
      case FilterCategory::Blur:
        return filter_blur_menu;
      case FilterCategory::Sharpen:
        return filter_sharpen_menu;
      case FilterCategory::Distort:
        return filter_distort_menu;
      case FilterCategory::Noise:
        return filter_noise_menu;
      case FilterCategory::Pixelate:
        return filter_pixelate_menu;
      case FilterCategory::Stylize:
        return filter_stylize_menu;
      case FilterCategory::Render:
        return filter_render_menu;
      case FilterCategory::Artistic:
        return filter_artistic_menu;
      case FilterCategory::Uncategorized:
      case FilterCategory::Adjustment:
        return filter_menu;
    }
    return filter_menu;
  };

  for (const auto& filter : filters_.filters()) {
    const auto identifier = QString::fromStdString(filter.identifier);
    if (is_adjustment_only_filter(filter)) {
      continue;
    }
    const auto display_name = filter_display_name(filter);
    auto* action = menu_for_filter(filter.catalog.category)
                       ->addAction(escape_qaction_ampersands(display_name));
    action->setObjectName(filter_action_object_name(identifier));
    action->setProperty("patchy.channelViewBlocked", true);
    action->setIcon(simple_icon(display_name.left(3).toUpper()));
    action->setStatusTip(tr("Apply %1 to the active layer").arg(display_name));
    refresh_action_tooltip(action);
    QPointer<QAction> filter_action(action);
    register_retranslation([this, filter_action, identifier] {
      if (filter_action == nullptr) {
        return;
      }
      const auto* filter = filters_.find(identifier.toStdString());
      if (filter == nullptr) {
        return;
      }
      const auto display_name = filter_display_name(*filter);
      filter_action->setText(escape_qaction_ampersands(display_name));
      filter_action->setIcon(simple_icon(display_name.left(3).toUpper()));
      filter_action->setStatusTip(tr("Apply %1 to the active layer").arg(display_name));
      refresh_action_tooltip(filter_action);
    });
    connect(action, &QAction::triggered, this, [this, identifier] { apply_filter(identifier); });
    register_document_action(action);
  }

  auto* scan_legacy_plugins_action = plugins_menu->addAction(tr("&Scan Legacy Photoshop Plug-ins..."));
  scan_legacy_plugins_action->setObjectName(QStringLiteral("pluginsScanLegacyAction"));
  scan_legacy_plugins_action->setIcon(simple_icon(QStringLiteral("8BF")));
  connect(scan_legacy_plugins_action, &QAction::triggered, this, [this] { scan_legacy_plugins(); });
#ifndef Q_OS_WIN
  // 8BF plug-ins are Windows binaries (the probe rejects them here with the same
  // message); a disabled note manages expectations up front.
  auto* legacy_windows_only_note = plugins_menu->addAction(tr("Legacy 8BF plug-ins run on Windows only"));
  legacy_windows_only_note->setObjectName(QStringLiteral("pluginsLegacyWindowsOnlyNote"));
  legacy_windows_only_note->setEnabled(false);
  bind_action_text(legacy_windows_only_note, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Legacy 8BF plug-ins run on Windows only"));
#endif
  legacy_plugins_menu_ = plugins_menu->addMenu(tr("Legacy Photoshop Plug-ins"));
  legacy_plugins_menu_->setObjectName(QStringLiteral("legacyPluginsMenu"));

  auto* zoom_in = view_menu->addAction(tr("Zoom &In"));
  auto* zoom_out = view_menu->addAction(tr("Zoom &Out"));
  auto* fit_on_screen = view_menu->addAction(tr("&Fit on Screen"));
  auto* zoom_reset = view_menu->addAction(tr("&Actual Pixels"));
  view_vector_preview_action_ = view_menu->addAction(tr("Dynamic Vector Preview"));
  view_vector_preview_action_->setObjectName(QStringLiteral("viewVectorPreviewAction"));
  view_vector_preview_action_->setCheckable(true);
  view_vector_preview_action_->setChecked(view_vector_preview_enabled_);
  register_hotkey(view_vector_preview_action_, "view.vector_preview");
  connect(view_vector_preview_action_, &QAction::toggled, this, [this](bool checked) {
    view_vector_preview_enabled_ = checked;
    for (const auto& session : sessions_) {
      session->canvas->set_vector_preview_enabled(checked);
    }
    save_view_settings();
    refresh_vector_preview_action();
  });
  auto* selection_edges_action = view_menu->addAction(tr("Show Selection &Edges"));
  auto* target_path_action = view_menu->addAction(tr("Show Target &Path"));
  view_menu->addSeparator();
  view_rulers_action_ = view_menu->addAction(tr("&Rulers"));
  view_grid_action_ = view_menu->addAction(tr("&Grid"));
  view_guides_action_ = view_menu->addAction(tr("&Guides"));
  view_snap_action_ = view_menu->addAction(tr("&Snap"));
  view_lock_guides_action_ = view_menu->addAction(tr("Lock Guides"));
  auto* snap_to_menu = view_menu->addMenu(tr("Snap &To"));
  view_snap_guides_action_ = snap_to_menu->addAction(tr("Guides"));
  view_snap_grid_action_ = snap_to_menu->addAction(tr("Grid"));
  view_snap_document_action_ = snap_to_menu->addAction(tr("Document Bounds and Center"));
  view_snap_layers_action_ = snap_to_menu->addAction(tr("Layer Bounds and Centers"));
  view_snap_selection_action_ = snap_to_menu->addAction(tr("Selection Bounds and Center"));
  auto* guides_menu = view_menu->addMenu(tr("Guide Operations"));
  auto* new_guide_action = guides_menu->addAction(tr("New Guide..."));
  auto* new_guide_layout_action = guides_menu->addAction(tr("New Guide Layout..."));
  auto* clear_selected_guides_action = guides_menu->addAction(tr("Clear Selected Guides"));
  auto* clear_guides_action = guides_menu->addAction(tr("Clear Guides"));
  view_menu->addSeparator();
  auto* tile_preview_action = view_menu->addAction(tr("Seamless &Tile Preview"));
  tile_preview_action->setObjectName(QStringLiteral("viewTilePreviewAction"));
  tile_preview_action->setCheckable(true);
  register_hotkey(tile_preview_action, "view.tile_preview");
  connect(tile_preview_action, &QAction::toggled, this,
          [this, tile_preview_action](bool checked) { set_tile_preview_visible(checked, tile_preview_action); });
  tiling_mode_action_ = view_menu->addAction(tr("Seamless Tiling in &Window"));
  tiling_mode_action_->setObjectName(QStringLiteral("viewTilingModeAction"));
  tiling_mode_action_->setCheckable(true);
  tiling_mode_action_->setStatusTip(tr("Repeat the document around itself in the window so tile seams are visible while painting"));
  bind_translated_status_tip(
      tiling_mode_action_,
      "Repeat the document around itself in the window so tile seams are visible while painting");
  register_hotkey(tiling_mode_action_, "view.show_tiling");
  // triggered (not toggled): activate_document_tab syncs the check from each canvas's
  // per-document state with setChecked, which must not re-apply to the canvas.
  connect(tiling_mode_action_, &QAction::triggered, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_tiling_preview_enabled(checked);
    }
  });
  register_document_action(tiling_mode_action_);
  zoom_in->setObjectName(QStringLiteral("viewZoomInAction"));
  zoom_out->setObjectName(QStringLiteral("viewZoomOutAction"));
  fit_on_screen->setObjectName(QStringLiteral("viewFitOnScreenAction"));
  zoom_reset->setObjectName(QStringLiteral("viewActualPixelsAction"));
  selection_edges_action->setObjectName(QStringLiteral("viewToggleSelectionEdgesAction"));
  target_path_action->setObjectName(QStringLiteral("viewToggleTargetPathAction"));
  view_rulers_action_->setObjectName(QStringLiteral("viewToggleRulersAction"));
  view_grid_action_->setObjectName(QStringLiteral("viewToggleGridAction"));
  view_guides_action_->setObjectName(QStringLiteral("viewToggleGuidesAction"));
  view_snap_action_->setObjectName(QStringLiteral("viewToggleSnapAction"));
  view_lock_guides_action_->setObjectName(QStringLiteral("viewLockGuidesAction"));
  snap_to_menu->setObjectName(QStringLiteral("viewSnapToMenu"));
  guides_menu->setObjectName(QStringLiteral("viewGuideOperationsMenu"));
  view_snap_guides_action_->setObjectName(QStringLiteral("viewSnapToGuidesAction"));
  view_snap_grid_action_->setObjectName(QStringLiteral("viewSnapToGridAction"));
  view_snap_document_action_->setObjectName(QStringLiteral("viewSnapToDocumentAction"));
  view_snap_layers_action_->setObjectName(QStringLiteral("viewSnapToLayersAction"));
  view_snap_selection_action_->setObjectName(QStringLiteral("viewSnapToSelectionAction"));
  new_guide_action->setObjectName(QStringLiteral("viewNewGuideAction"));
  new_guide_layout_action->setObjectName(QStringLiteral("viewNewGuideLayoutAction"));
  clear_selected_guides_action->setObjectName(QStringLiteral("viewClearSelectedGuidesAction"));
  clear_guides_action->setObjectName(QStringLiteral("viewClearGuidesAction"));
  zoom_in->setIcon(simple_icon(QStringLiteral("zoomIn")));
  zoom_out->setIcon(simple_icon(QStringLiteral("zoomOut")));
  fit_on_screen->setIcon(simple_icon(QStringLiteral("fit")));
  zoom_reset->setIcon(simple_icon(QStringLiteral("1x")));
  selection_edges_action->setIcon(simple_icon(QStringLiteral("SE")));
  view_rulers_action_->setIcon(simple_icon(QStringLiteral("RU")));
  view_grid_action_->setIcon(simple_icon(QStringLiteral("GRD")));
  view_guides_action_->setIcon(simple_icon(QStringLiteral("GDE")));
  view_snap_action_->setIcon(simple_icon(QStringLiteral("SN")));
  view_lock_guides_action_->setIcon(simple_icon(QStringLiteral("LK")));
  new_guide_action->setIcon(simple_icon(QStringLiteral("NG")));
  new_guide_layout_action->setIcon(simple_icon(QStringLiteral("NGL")));
  clear_selected_guides_action->setIcon(simple_icon(QStringLiteral("CSG")));
  clear_guides_action->setIcon(simple_icon(QStringLiteral("CG")));
  target_path_action->setCheckable(true);
  target_path_action->setChecked(view_target_path_visible_);
  view_rulers_action_->setCheckable(true);
  view_grid_action_->setCheckable(true);
  view_guides_action_->setCheckable(true);
  view_snap_action_->setCheckable(true);
  view_lock_guides_action_->setCheckable(true);
  view_snap_guides_action_->setCheckable(true);
  view_snap_grid_action_->setCheckable(true);
  view_snap_document_action_->setCheckable(true);
  view_snap_layers_action_->setCheckable(true);
  view_snap_selection_action_->setCheckable(true);
  view_rulers_action_->setChecked(view_rulers_visible_);
  view_grid_action_->setChecked(view_grid_visible_);
  view_guides_action_->setChecked(view_guides_visible_);
  view_snap_action_->setChecked(view_snap_enabled_);
  view_lock_guides_action_->setChecked(view_guides_locked_);
  view_snap_guides_action_->setChecked(view_snap_to_guides_);
  view_snap_grid_action_->setChecked(view_snap_to_grid_);
  view_snap_document_action_->setChecked(view_snap_to_document_);
  view_snap_layers_action_->setChecked(view_snap_to_layers_);
  view_snap_selection_action_->setChecked(view_snap_to_selection_);
  auto zoom_in_defaults = QKeySequence::keyBindings(QKeySequence::ZoomIn);
  if (!zoom_in_defaults.contains(QKeySequence(Qt::CTRL | Qt::Key_Equal))) {
    zoom_in_defaults << QKeySequence(Qt::CTRL | Qt::Key_Equal);
  }
  register_hotkey(zoom_in, "view.zoom_in", zoom_in_defaults);
  register_hotkey(zoom_out, "view.zoom_out", QKeySequence::keyBindings(QKeySequence::ZoomOut));
  register_hotkey(fit_on_screen, "view.fit_on_screen", QKeySequence(Qt::CTRL | Qt::Key_0));
  register_hotkey(zoom_reset, "view.actual_pixels", QKeySequence(Qt::CTRL | Qt::Key_1));
  register_hotkey(selection_edges_action, "view.selection_edges", QKeySequence(Qt::CTRL | Qt::Key_H));
  register_hotkey(target_path_action, "view.target_path",
                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
  register_hotkey(view_rulers_action_, "view.rulers", QKeySequence(Qt::CTRL | Qt::Key_R));
  register_hotkey(view_grid_action_, "view.grid", QKeySequence(Qt::CTRL | Qt::Key_Apostrophe));
  register_hotkey(view_guides_action_, "view.guides", QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
  register_hotkey(view_snap_action_, "view.snap", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon));
  register_hotkey(view_lock_guides_action_, "view.lock_guides", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Semicolon));
  register_hotkey(new_guide_action, "view.new_guide");
  register_hotkey(new_guide_layout_action, "view.new_guide_layout");
  register_hotkey(clear_selected_guides_action, "view.clear_selected_guides");
  register_hotkey(clear_guides_action, "view.clear_guides");
  connect(zoom_in, &QAction::triggered, this, [this] { canvas_->set_zoom_centered(canvas_->zoom() * 1.25); });
  connect(zoom_out, &QAction::triggered, this, [this] { canvas_->set_zoom_centered(canvas_->zoom() * 0.8); });
  connect(fit_on_screen, &QAction::triggered, this, [this] { canvas_->fit_to_view(); });
  connect(zoom_reset, &QAction::triggered, this, [this] { canvas_->set_zoom_centered(1.0); });
  connect(selection_edges_action, &QAction::triggered, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->toggle_selection_edges_visible();
    }
  });
  connect(target_path_action, &QAction::toggled, this, [this](bool checked) {
    view_target_path_visible_ = checked;
    // Deliberately not persisted: every launch starts visible (Photoshop).
    for (const auto& active_session : sessions_) {
      if (active_session->canvas != nullptr) {
        active_session->canvas->set_target_path_visible(checked);
      }
    }
  });
  const auto apply_view_settings = [this] {
    for (const auto& active_session : sessions_) {
      apply_canvas_aid_settings(active_session->canvas);
    }
    save_view_settings();
  };
  connect(view_rulers_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_rulers_visible_ = checked;
    apply_view_settings();
  });
  connect(view_grid_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_grid_visible_ = checked;
    apply_view_settings();
  });
  connect(view_guides_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_guides_visible_ = checked;
    apply_view_settings();
  });
  connect(view_snap_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_enabled_ = checked;
    apply_view_settings();
  });
  connect(view_lock_guides_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_guides_locked_ = checked;
    apply_view_settings();
  });
  connect(view_snap_guides_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_guides_ = checked;
    apply_view_settings();
  });
  connect(view_snap_grid_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_grid_ = checked;
    apply_view_settings();
  });
  connect(view_snap_document_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_document_ = checked;
    apply_view_settings();
  });
  connect(view_snap_layers_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_layers_ = checked;
    apply_view_settings();
  });
  connect(view_snap_selection_action_, &QAction::toggled, this, [this, apply_view_settings](bool checked) {
    view_snap_to_selection_ = checked;
    apply_view_settings();
  });
  connect(new_guide_action, &QAction::triggered, this, [this] { new_guide_dialog(); });
  connect(new_guide_layout_action, &QAction::triggered, this, [this] { new_guide_layout_dialog(); });
  connect(clear_selected_guides_action, &QAction::triggered, this, [this] { clear_selected_guides(); });
  connect(clear_guides_action, &QAction::triggered, this, [this] { clear_guides(); });
  for (auto* action : {zoom_in, zoom_out, fit_on_screen, zoom_reset, selection_edges_action, view_rulers_action_,
                       view_grid_action_, view_guides_action_, view_snap_action_, view_lock_guides_action_,
                       snap_to_menu->menuAction(), view_snap_guides_action_, view_snap_grid_action_,
                       view_snap_document_action_, view_snap_layers_action_, view_snap_selection_action_,
                       guides_menu->menuAction(), new_guide_action, new_guide_layout_action,
                       clear_selected_guides_action, clear_guides_action}) {
    register_document_action(action);
  }

  float_document_action_ = window_menu->addAction(tr("Float in &Window"));
  float_document_action_->setObjectName(QStringLiteral("windowFloatDocumentAction"));
  bind_action_text(float_document_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Float in &Window"));
  register_hotkey(float_document_action_, "window.float_document", QKeySequence());
  connect(float_document_action_, &QAction::triggered, this, [this] { float_active_document(); });
  register_document_action(float_document_action_);

  dock_document_action_ = window_menu->addAction(tr("&Dock to Tabs"));
  dock_document_action_->setObjectName(QStringLiteral("windowDockDocumentAction"));
  bind_action_text(dock_document_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Dock to Tabs"));
  register_hotkey(dock_document_action_, "window.dock_document", QKeySequence());
  connect(dock_document_action_, &QAction::triggered, this, [this] { dock_active_document(); });
  register_document_action(dock_document_action_);

  float_all_action_ = window_menu->addAction(tr("Float A&ll in Windows"));
  float_all_action_->setObjectName(QStringLiteral("windowFloatAllAction"));
  bind_action_text(float_all_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Float A&ll in Windows"));
  register_hotkey(float_all_action_, "window.float_all", QKeySequence());
  connect(float_all_action_, &QAction::triggered, this, [this] { float_all_documents(); });
  register_document_action(float_all_action_);

  consolidate_tabs_action_ = window_menu->addAction(tr("&Consolidate All to Tabs"));
  consolidate_tabs_action_->setObjectName(QStringLiteral("windowConsolidateTabsAction"));
  bind_action_text(consolidate_tabs_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Consolidate All to Tabs"));
  register_hotkey(consolidate_tabs_action_, "window.consolidate_all_to_tabs", QKeySequence());
  connect(consolidate_tabs_action_, &QAction::triggered, this, [this] { consolidate_all_to_tabs(); });
  register_document_action(consolidate_tabs_action_);

  window_menu->addSeparator();

  tile_windows_action_ = window_menu->addAction(tr("&Tile"));
  tile_windows_action_->setObjectName(QStringLiteral("windowTileAction"));
  bind_action_text(tile_windows_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "&Tile"));
  register_hotkey(tile_windows_action_, "window.tile_windows", QKeySequence());
  connect(tile_windows_action_, &QAction::triggered, this, [this] { tile_float_windows(); });
  register_document_action(tile_windows_action_);

  cascade_windows_action_ = window_menu->addAction(tr("Ca&scade"));
  cascade_windows_action_->setObjectName(QStringLiteral("windowCascadeAction"));
  bind_action_text(cascade_windows_action_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Ca&scade"));
  register_hotkey(cascade_windows_action_, "window.cascade_windows", QKeySequence());
  connect(cascade_windows_action_, &QAction::triggered, this, [this] { cascade_float_windows(); });
  register_document_action(cascade_windows_action_);

#ifdef Q_OS_WASM
  // Float windows are off on wasm (float_document_session no-ops; a browser
  // tab cannot host separate movable windows). The actions stay registered so
  // hotkey ids and wiring are stable; hidden actions do not render in the menu.
  float_document_action_->setVisible(false);
  dock_document_action_->setVisible(false);
  float_all_action_->setVisible(false);
  consolidate_tabs_action_->setVisible(false);
  tile_windows_action_->setVisible(false);
  cascade_windows_action_->setVisible(false);
#endif

  window_menu->addSeparator();

  auto* screen_size_menu = window_menu->addMenu(tr("Set Screen Size"));
  screen_size_menu->setObjectName(QStringLiteral("windowSetScreenSizeMenu"));
  struct ScreenSizePreset {
    int width;
    int height;
    const char* label;
  };
  static constexpr ScreenSizePreset kScreenSizePresets[] = {
      {1280, 720, QT_TR_NOOP("1280 x 720 (HD)")},     {1366, 768, QT_TR_NOOP("1366 x 768")},
      {1600, 900, QT_TR_NOOP("1600 x 900")},          {1920, 1080, QT_TR_NOOP("1920 x 1080 (Full HD)")},
      {2560, 1440, QT_TR_NOOP("2560 x 1440 (QHD)")},  {3840, 2160, QT_TR_NOOP("3840 x 2160 (4K UHD)")},
  };
  for (const auto& preset : kScreenSizePresets) {
    auto* action = screen_size_menu->addAction(QString());
    action->setObjectName(QStringLiteral("windowSetScreenSize%1x%2Action").arg(preset.width).arg(preset.height));
    bind_action_text(action, preset.label);
    connect(action, &QAction::triggered, this,
            [this, preset] { set_window_screen_size(QSize(preset.width, preset.height)); });
  }

  auto* force_refresh_action = window_menu->addAction(tr("Force Refresh"));
  force_refresh_action->setObjectName(QStringLiteral("windowForceRefreshAction"));
  force_refresh_action->setIcon(simple_icon(QStringLiteral("RF")));
  register_hotkey(force_refresh_action, "window.force_refresh", QKeySequence(Qt::Key_F5));
  connect(force_refresh_action, &QAction::triggered, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->force_refresh();
    }
  });
  register_document_action(force_refresh_action);

  // Photoshop lists the open documents at the bottom of the Window menu. The
  // entries are rebuilt each time the menu opens (dynamic actions, no hotkey
  // ids), so a floated or hidden document is one click away.
  window_documents_separator_ = window_menu->addSeparator();
  connect(window_menu, &QMenu::aboutToShow, this, [this, window_menu] {
    rebuild_window_document_entries(window_menu);
  });

  auto* scripting_guide_action = help_menu->addAction(tr("&Scripting Guide"));
  scripting_guide_action->setObjectName(QStringLiteral("helpScriptingGuideAction"));
  register_hotkey(scripting_guide_action, "help.scripting_guide");
  connect(scripting_guide_action, &QAction::triggered, this, [this] { open_scripting_guide(); });

  auto* ai_setup_action = help_menu->addAction(tr("Set &up AI Control..."));
  ai_setup_action->setObjectName(QStringLiteral("helpAiSetupAction"));
  ai_setup_action->setMenuRole(QAction::NoRole);
  register_hotkey(ai_setup_action, "help.ai_setup");
  connect(ai_setup_action, &QAction::triggered, this, [this] { open_ai_setup_dialog(); });
#ifdef Q_OS_WASM
  // The browser build ships no connector; hide the entry but keep the command id
  // registered so saved hotkey tables stay stable across platforms.
  ai_setup_action->setVisible(false);
#endif

  auto* about_action = help_menu->addAction(tr("&About Patchy"));
  about_action->setMenuRole(QAction::AboutRole);
  connect(about_action, &QAction::triggered, this, [this] { show_about(); });

  // Export the cross-phase locals: build_tool_palette() appends the Type tool
  // to type_menu, and bind_action_translations() binds translation sources to
  // the rest.
  ctx.type_menu = type_menu;
  ctx.window_menu = window_menu;
  ctx.new_action = new_action;
  ctx.open_action = open_action;
  ctx.save_action = save_action;
  ctx.save_as_action = save_as_action;
  ctx.export_flat_action = export_flat_action;
  ctx.page_setup_action = page_setup_action;
  ctx.print_action = print_action;
  ctx.close_action = close_action;
  ctx.close_all_action = close_all_action;
  ctx.preferences_action = preferences_action;
  ctx.quit_action = quit_action;
  ctx.cut_action = cut_action;
  ctx.copy_action = copy_action;
  ctx.copy_merged_action = copy_merged_action;
  ctx.paste_action = paste_action;
  ctx.transform_action = transform_action;
  ctx.select_all_action = select_all_action;
  ctx.clear_selection_action = clear_selection_action;
  ctx.reselect_action = reselect_action;
  ctx.inverse_selection_action = inverse_selection_action;
  ctx.grow_selection_action = grow_selection_action;
  ctx.similar_selection_action = similar_selection_action;
  ctx.expand_selection_action = expand_selection_action;
  ctx.contract_selection_action = contract_selection_action;
  ctx.border_selection_action = border_selection_action;
  ctx.layer_transparency_action = layer_transparency_action;
  ctx.stroke_selection_action = stroke_selection_action;
  ctx.remove_object_action = remove_object_action;
  ctx.define_brush_tip_action = define_brush_tip_action;
  remove_object_action_ = remove_object_action;
  // The canvas context menu's selection section (a right-click on the
  // selection): the menus' own QActions, so hotkeys and enable state stay in
  // step; nullptr is a separator.
  selection_context_actions_ = {remove_object_action, nullptr,
                                fill_layer_action,    clear_layer_action,
                                stroke_selection_action, nullptr,
                                clear_selection_action, inverse_selection_action};
  ctx.add_layer_action = add_layer_action;
  ctx.add_folder_action = add_folder_action;
  ctx.layer_new_menu = layer_new_menu;
  ctx.new_adjustment_layer_menu = new_adjustment_layer_menu;
  ctx.new_fill_layer_menu = new_fill_layer_menu;
  ctx.layer_mask_menu = layer_mask_menu;
  ctx.vector_mask_menu = vector_mask_menu;
  ctx.layer_smart_objects_menu = layer_smart_objects_menu;
  ctx.layer_arrange_menu = layer_arrange_menu;
  ctx.layer_align_menu = layer_align_menu;
  ctx.layer_distribute_menu = layer_distribute_menu;
  ctx.layer_via_copy_action = layer_via_copy_action;
  ctx.layer_via_cut_action = layer_via_cut_action;
  ctx.add_mask_action = add_mask_action;
  ctx.edit_adjustment_action = edit_adjustment_action;
  ctx.duplicate_layer_action = duplicate_layer_action;
  ctx.merge_visible_action = merge_visible_action;
  ctx.merge_down_action = merge_down_action;
  ctx.rename_layer_action = rename_layer_action;
  ctx.delete_layer_action = delete_layer_action;
  ctx.fill_layer_action = fill_layer_action;
  ctx.fill_background_action = fill_background_action;
  ctx.clear_layer_action = clear_layer_action;
  ctx.flip_h_action = flip_h_action;
  ctx.flip_v_action = flip_v_action;
  ctx.layer_up_action = layer_up_action;
  ctx.layer_down_action = layer_down_action;
  ctx.adjustments_menu = adjustments_menu;
  ctx.levels_action = levels_action;
  ctx.curves_action = curves_action;
  ctx.hue_saturation_action = hue_saturation_action;
  ctx.color_balance_action = color_balance_action;
  ctx.image_size_action = image_size_action;
  ctx.canvas_size_action = canvas_size_action;
  ctx.crop_action = crop_action;
  ctx.rotate_cw_action = rotate_cw_action;
  ctx.rotate_ccw_action = rotate_ccw_action;
  ctx.rotate_arbitrary_action = rotate_arbitrary_action;
  ctx.shift_seams_action = shift_seams_action;
  ctx.scan_legacy_plugins_action = scan_legacy_plugins_action;
  ctx.zoom_in = zoom_in;
  ctx.zoom_out = zoom_out;
  ctx.fit_on_screen = fit_on_screen;
  ctx.zoom_reset = zoom_reset;
  ctx.selection_edges_action = selection_edges_action;
  ctx.target_path_action = target_path_action;
  ctx.tile_preview_action = tile_preview_action;
  ctx.snap_to_menu = snap_to_menu;
  ctx.guides_menu = guides_menu;
  ctx.new_guide_action = new_guide_action;
  ctx.new_guide_layout_action = new_guide_layout_action;
  ctx.clear_selected_guides_action = clear_selected_guides_action;
  ctx.clear_guides_action = clear_guides_action;
  ctx.screen_size_menu = screen_size_menu;
  ctx.force_refresh_action = force_refresh_action;
  ctx.scripting_guide_action = scripting_guide_action;
  ctx.about_action = about_action;
  ctx.ai_setup_action = ai_setup_action;
}

}  // namespace patchy::ui
