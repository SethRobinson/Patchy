// MainWindow's preferences and settings implementation, split out of
// main_window.cpp: the Preferences dialog (show_preferences), the guide
// dialogs (new guide, guide layout, clear guides), and the settings
// load/save/apply members (ruler units, canvas aids, pen input, view
// settings). Pure function moves from main_window.cpp; behavior must
// stay identical.

#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
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
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
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
#include <QCursor>
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
#include <QElapsedTimer>
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

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

QString pen_button_action_to_token(PenButtonAction action) {
  switch (action) {
    case PenButtonAction::None:
      return QStringLiteral("none");
    case PenButtonAction::PanCanvas:
      return QStringLiteral("pan");
    case PenButtonAction::ZoomCanvas:
      return QStringLiteral("zoom");
    case PenButtonAction::PickColor:
      return QStringLiteral("pickColor");
    case PenButtonAction::SetCloneSource:
      return QStringLiteral("setCloneSource");
    case PenButtonAction::SwapColors:
      return QStringLiteral("swapColors");
    case PenButtonAction::Undo:
      return QStringLiteral("undo");
    case PenButtonAction::Redo:
      return QStringLiteral("redo");
    case PenButtonAction::ToggleEraser:
      return QStringLiteral("toggleEraser");
    case PenButtonAction::IncreaseBrushSize:
      return QStringLiteral("increaseBrushSize");
    case PenButtonAction::DecreaseBrushSize:
      return QStringLiteral("decreaseBrushSize");
  }
  return QStringLiteral("none");
}

PenButtonAction pen_button_action_from_token(const QString& token) {
  if (token == QStringLiteral("pan")) {
    return PenButtonAction::PanCanvas;
  }
  if (token == QStringLiteral("zoom")) {
    return PenButtonAction::ZoomCanvas;
  }
  if (token == QStringLiteral("pickColor")) {
    return PenButtonAction::PickColor;
  }
  if (token == QStringLiteral("setCloneSource")) {
    return PenButtonAction::SetCloneSource;
  }
  if (token == QStringLiteral("swapColors")) {
    return PenButtonAction::SwapColors;
  }
  if (token == QStringLiteral("undo")) {
    return PenButtonAction::Undo;
  }
  if (token == QStringLiteral("redo")) {
    return PenButtonAction::Redo;
  }
  if (token == QStringLiteral("toggleEraser")) {
    return PenButtonAction::ToggleEraser;
  }
  if (token == QStringLiteral("increaseBrushSize")) {
    return PenButtonAction::IncreaseBrushSize;
  }
  if (token == QStringLiteral("decreaseBrushSize")) {
    return PenButtonAction::DecreaseBrushSize;
  }
  return PenButtonAction::None;
}

void fill_alpha_checkerboard(QPainter& painter, const QRect& rect, int cell_size) {
  if (rect.isEmpty() || cell_size <= 0) {
    return;
  }
  const QColor dark(44, 44, 44);
  const QColor light(188, 188, 188);
  for (int y = rect.top(); y <= rect.bottom(); y += cell_size) {
    for (int x = rect.left(); x <= rect.right(); x += cell_size) {
      const QRect cell(x, y, std::min(cell_size, rect.right() - x + 1),
                       std::min(cell_size, rect.bottom() - y + 1));
      const auto parity = ((x - rect.left()) / cell_size + (y - rect.top()) / cell_size) % 2;
      painter.fillRect(cell, parity == 0 ? dark : light);
    }
  }
}

int color_alpha_percent(QColor color) {
  return std::clamp(static_cast<int>(std::lround(color.alphaF() * 100.0)), 0, 100);
}

QString overlay_color_summary_text(QColor color) {
  return QStringLiteral("%1  %2%").arg(color.name(QColor::HexRgb).toUpper()).arg(color_alpha_percent(color));
}

QIcon overlay_color_swatch_icon(QColor color) {
  QPixmap pixmap(48, 24);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  const QRect swatch = pixmap.rect().adjusted(1, 1, -2, -2);
  fill_alpha_checkerboard(painter, swatch, 6);
  painter.fillRect(swatch, color);
  painter.setPen(QPen(QColor(12, 12, 12), 1));
  painter.drawRect(swatch.adjusted(0, 0, -1, -1));
  return QIcon(pixmap);
}

QPixmap grid_overlay_preview_pixmap(QColor grid_color, QColor guide_color, int grid_style, int subdivisions) {
  QPixmap pixmap(218, 86);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, false);
  const QRect preview_rect = pixmap.rect().adjusted(1, 1, -2, -2);
  fill_alpha_checkerboard(painter, preview_rect, 14);
  painter.fillRect(QRect(preview_rect.left(), preview_rect.top(), preview_rect.width() / 2, preview_rect.height()),
                   QColor(28, 30, 34, 130));
  painter.fillRect(QRect(preview_rect.left() + preview_rect.width() / 2, preview_rect.top(),
                         preview_rect.width() - preview_rect.width() / 2, preview_rect.height()),
                   QColor(238, 238, 238, 110));

  auto minor_color = grid_color;
  minor_color.setAlpha(std::clamp(grid_color.alpha() / 2, 24, 120));
  auto major_color = grid_color;
  major_color.setAlpha(std::clamp(grid_color.alpha(), 45, 220));

  const int safe_subdivisions = std::clamp(subdivisions, 1, 64);
  constexpr int major_spacing = 48;
  const int minor_spacing = std::max(6, major_spacing / safe_subdivisions);
  const auto draw_grid = [&](int spacing, QColor color, Qt::PenStyle style) {
    if (spacing <= 0) {
      return;
    }
    QPen pen(color, 1.0, style);
    pen.setCosmetic(true);
    painter.setPen(pen);
    for (int x = preview_rect.left(); x <= preview_rect.right(); x += spacing) {
      painter.drawLine(QPoint(x, preview_rect.top()), QPoint(x, preview_rect.bottom()));
    }
    for (int y = preview_rect.top(); y <= preview_rect.bottom(); y += spacing) {
      painter.drawLine(QPoint(preview_rect.left(), y), QPoint(preview_rect.right(), y));
    }
  };
  if (safe_subdivisions > 1) {
    draw_grid(minor_spacing, minor_color, grid_style == 0 ? Qt::DotLine : Qt::DashLine);
  }
  draw_grid(major_spacing, major_color, grid_style == 0 ? Qt::SolidLine : Qt::DotLine);

  QPen guide_pen(guide_color, 2.0, Qt::SolidLine);
  guide_pen.setCosmetic(true);
  painter.setPen(guide_pen);
  const int vertical_guide = preview_rect.left() + (preview_rect.width() * 2) / 3;
  const int horizontal_guide = preview_rect.top() + preview_rect.height() / 2;
  painter.drawLine(QPoint(vertical_guide, preview_rect.top()), QPoint(vertical_guide, preview_rect.bottom()));
  painter.drawLine(QPoint(preview_rect.left(), horizontal_guide), QPoint(preview_rect.right(), horizontal_guide));

  painter.setPen(QPen(QColor(92, 92, 92), 1));
  painter.drawRect(preview_rect.adjusted(0, 0, -1, -1));
  return pixmap;
}

}  // namespace

void MainWindow::show_preferences() {
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("patchyPreferencesDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, tr("Preferences"));

  auto settings = app_settings();
  dialog.setMinimumSize(650, 430);
  dialog.resize(700, 560);

  const auto make_tab_page = [](QWidget* parent) {
    // Wrap each tab in a scroll area so a tab whose content is taller than the
    // dialog scrolls instead of overlapping its own controls.
    auto* scroll = new QScrollArea(parent);
    scroll->setObjectName(QStringLiteral("preferencesTabScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page = new QWidget(scroll);
    page->setObjectName(QStringLiteral("preferencesTabPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    scroll->setWidget(page);
    return std::pair<QWidget*, QVBoxLayout*>{scroll, layout};
  };
  const auto configure_panel = [](QFrame* panel) {
    panel->setProperty("preferencesPanel", true);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  };
  const auto configure_form = [](QFormLayout* form) {
    form->setContentsMargins(12, 12, 12, 12);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  };

  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName(QStringLiteral("preferencesTabWidget"));
  tabs->setDocumentMode(true);
  suppress_native_tab_bar_base(*tabs);

  auto [application_page, application_layout] = make_tab_page(tabs);
  auto* application_group = new QFrame(application_page);
  application_group->setObjectName(QStringLiteral("preferencesApplicationGroup"));
  configure_panel(application_group);
  auto* application_form = new QFormLayout(application_group);
  configure_form(application_form);

  auto* language_combo = new QComboBox(application_group);
  language_combo->setObjectName(QStringLiteral("preferencesLanguageCombo"));
  language_combo->addItem(tr("English"), QStringLiteral("en"));
  language_combo->addItem(QStringLiteral("日本語"), QStringLiteral("ja"));
  const auto current_language = LocalizationManager::instance().current_language();
  const auto current_index = language_combo->findData(current_language);
  language_combo->setCurrentIndex(current_index >= 0 ? current_index : 0);
  application_form->addRow(tr("Language:"), language_combo);

  auto* gui_scale_combo = new QComboBox(application_group);
  gui_scale_combo->setObjectName(QStringLiteral("preferencesGuiScaleCombo"));
  constexpr std::array<int, 5> gui_scale_percents{100, 125, 150, 175, 200};
  for (const int percent : gui_scale_percents) {
    gui_scale_combo->addItem(QStringLiteral("%1%").arg(percent), percent);
  }
  const int current_gui_scale =
      std::clamp(settings.value(QStringLiteral("preferences/guiScalePercent"), 100).toInt(),
                 gui_scale_percents.front(), gui_scale_percents.back());
  const int gui_scale_index = gui_scale_combo->findData(current_gui_scale);
  gui_scale_combo->setCurrentIndex(gui_scale_index >= 0 ? gui_scale_index : 0);
  application_form->addRow(tr("Interface scale:"), gui_scale_combo);

  auto* update_check = new QCheckBox(tr("Check for updates on startup"), application_group);
  update_check->setObjectName(QStringLiteral("preferencesCheckForUpdatesCheck"));
  update_check->setChecked(settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());
  application_form->addRow(update_check);
  auto* psd_import_warnings_check =
      new QCheckBox(tr("Show import warnings and notes in a popup (status bar otherwise)"), application_group);
  psd_import_warnings_check->setObjectName(QStringLiteral("preferencesShowPsdImportWarningsCheck"));
  psd_import_warnings_check->setToolTip(
      tr("When enabled, opening a file shows the PSD compatibility report and an Import Notes popup. "
         "When disabled, import notes appear only in the status bar."));
  psd_import_warnings_check->setChecked(
      settings.value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool());
  application_form->addRow(psd_import_warnings_check);
  auto* raw_develop_check =
      new QCheckBox(tr("Show the develop dialog when opening camera raw files"), application_group);
  raw_develop_check->setObjectName(QStringLiteral("preferencesShowRawDevelopCheck"));
  raw_develop_check->setToolTip(
      tr("When disabled, camera raw files open immediately with neutral develop settings "
         "(as-shot white balance, no adjustments)."));
  raw_develop_check->setChecked(settings.value(QStringLiteral("imports/showRawDevelopDialog"), true).toBool());
  application_form->addRow(raw_develop_check);
  // Resets the "Do this for every indexed image" choice remembered by the
  // indexed-image adoption prompt.
  auto* indexed_open_combo = new QComboBox(application_group);
  indexed_open_combo->setObjectName(QStringLiteral("preferencesIndexedOpenCombo"));
  indexed_open_combo->addItem(tr("Ask every time"), QStringLiteral("ask"));
  indexed_open_combo->addItem(tr("Always use the palette"), QStringLiteral("always"));
  indexed_open_combo->addItem(tr("Always edit as RGB"), QStringLiteral("never"));
  const auto indexed_open_policy =
      settings.value(QStringLiteral("imports/adoptIndexedPalette"), QStringLiteral("ask")).toString();
  const auto indexed_open_index = indexed_open_combo->findData(indexed_open_policy);
  indexed_open_combo->setCurrentIndex(indexed_open_index >= 0 ? indexed_open_index : 0);
  application_form->addRow(tr("Opening indexed images:"), indexed_open_combo);
  application_layout->addWidget(application_group);

  // Development: the profiling stress test (see main_window_stress_test.cpp).
  auto* development_group = new QFrame(application_page);
  development_group->setObjectName(QStringLiteral("preferencesDevelopmentGroup"));
  configure_panel(development_group);
  auto* development_form = new QFormLayout(development_group);
  configure_form(development_form);
  auto* development_header = new QLabel(tr("Development"), development_group);
  auto development_header_font = development_header->font();
  development_header_font.setBold(true);
  development_header->setFont(development_header_font);
  development_form->addRow(development_header);
  auto* stress_info = new QLabel(
      tr("The profiling stress test builds a large scripted scene to measure rendering "
         "performance. It closes all open documents and takes several minutes. Primarily "
         "a development tool."),
      development_group);
  stress_info->setWordWrap(true);
  development_form->addRow(stress_info);
  auto* stress_size_combo = new QComboBox(development_group);
  stress_size_combo->setObjectName(QStringLiteral("preferencesStressSizeCombo"));
  stress_size_combo->addItem(tr("Quick (1024 px)"), stress_preset_token(StressPreset::Quick));
  stress_size_combo->addItem(tr("Small (2048 px)"), stress_preset_token(StressPreset::Small));
  stress_size_combo->addItem(tr("Standard (4096 px)"), stress_preset_token(StressPreset::Standard));
  stress_size_combo->addItem(tr("Huge (8192 px, needs lots of RAM)"), stress_preset_token(StressPreset::Huge));
  const auto stored_stress_preset =
      settings.value(QStringLiteral("development/stressPreset"), stress_preset_token(StressPreset::Quick))
          .toString();
  const auto stress_preset_index = stress_size_combo->findData(stored_stress_preset);
  stress_size_combo->setCurrentIndex(stress_preset_index >= 0 ? stress_preset_index : 0);
  development_form->addRow(tr("Stress test size:"), stress_size_combo);
  auto* run_stress_button = new QPushButton(tr("Run Profiling Stress Test..."), development_group);
  run_stress_button->setObjectName(QStringLiteral("preferencesRunStressTestButton"));
  development_form->addRow(run_stress_button);
  // Set by the run button; show_preferences starts the run only after the
  // dialog has closed and applied its settings (never from inside its nested
  // event loop).
  std::optional<StressPreset> pending_stress_preset;
  connect(run_stress_button, &QPushButton::clicked, &dialog,
          [&dialog, stress_size_combo, &pending_stress_preset] {
            const auto token = stress_size_combo->currentData().toString();
            auto stress_settings = app_settings();
            stress_settings.setValue(QStringLiteral("development/stressPreset"), token);
            pending_stress_preset = stress_preset_from_string(token).value_or(StressPreset::Standard);
            dialog.accept();
          });
  application_layout->addWidget(development_group);
  application_layout->addStretch(1);
  tabs->addTab(application_page, tr("Application"));

  connect(language_combo, &QComboBox::currentIndexChanged, &dialog, [this, language_combo] {
    const auto code = language_combo->currentData().toString();
    if (!code.isEmpty() && LocalizationManager::instance().set_language(code)) {
      refresh_language_actions();
    }
  });

  auto [pen_page, pen_layout] = make_tab_page(tabs);
  auto* pen_group = new QFrame(pen_page);
  pen_group->setObjectName(QStringLiteral("preferencesPenGroup"));
  configure_panel(pen_group);
  auto* pen_form = new QFormLayout(pen_group);
  configure_form(pen_form);

  auto* pen_enabled_check = new QCheckBox(tr("Enable pen and tablet input"), pen_group);
  pen_enabled_check->setObjectName(QStringLiteral("preferencesPenEnabledCheck"));
  pen_enabled_check->setChecked(pen_input_settings_.enabled);
  auto* pen_pressure_size_check = new QCheckBox(tr("Pressure controls brush size"), pen_group);
  pen_pressure_size_check->setObjectName(QStringLiteral("preferencesPenPressureSizeCheck"));
  pen_pressure_size_check->setChecked(pen_input_settings_.pressure_size);
  auto* pen_pressure_size_min_spin = new QSpinBox(pen_group);
  pen_pressure_size_min_spin->setObjectName(QStringLiteral("preferencesPenPressureSizeMinSpin"));
  pen_pressure_size_min_spin->setRange(1, 100);
  pen_pressure_size_min_spin->setSuffix(QStringLiteral("%"));
  pen_pressure_size_min_spin->setValue(pen_input_settings_.pressure_size_min_percent);
  auto* pen_pressure_opacity_check = new QCheckBox(tr("Pressure controls opacity"), pen_group);
  pen_pressure_opacity_check->setObjectName(QStringLiteral("preferencesPenPressureOpacityCheck"));
  pen_pressure_opacity_check->setChecked(pen_input_settings_.pressure_opacity);
  auto* pen_pressure_opacity_min_spin = new QSpinBox(pen_group);
  pen_pressure_opacity_min_spin->setObjectName(QStringLiteral("preferencesPenPressureOpacityMinSpin"));
  pen_pressure_opacity_min_spin->setRange(1, 100);
  pen_pressure_opacity_min_spin->setSuffix(QStringLiteral("%"));
  pen_pressure_opacity_min_spin->setValue(pen_input_settings_.pressure_opacity_min_percent);
  auto* pen_eraser_check = new QCheckBox(tr("Use eraser tip as Eraser"), pen_group);
  pen_eraser_check->setObjectName(QStringLiteral("preferencesPenEraserTipCheck"));
  pen_eraser_check->setChecked(pen_input_settings_.use_eraser_tip);
  auto* pen_wheel_zoom_check = new QCheckBox(tr("Scroll wheel zooms the canvas"), pen_group);
  pen_wheel_zoom_check->setObjectName(QStringLiteral("preferencesPenWheelZoomCheck"));
  pen_wheel_zoom_check->setChecked(wheel_zooms_);
  pen_wheel_zoom_check->setToolTip(
      tr("Also applies to a pen button set to Scroll. Hold Ctrl or Shift while scrolling to pan."));
  const auto populate_pen_button_combo = [](QComboBox* combo, PenButtonAction current) {
    const std::array<std::pair<PenButtonAction, QString>, 11> entries{{
        {PenButtonAction::None, tr("None")},
        {PenButtonAction::PanCanvas, tr("Pan canvas")},
        {PenButtonAction::ZoomCanvas, tr("Zoom canvas (drag)")},
        {PenButtonAction::PickColor, tr("Pick color")},
        {PenButtonAction::SetCloneSource, tr("Set clone source")},
        {PenButtonAction::SwapColors, tr("Swap colors")},
        {PenButtonAction::Undo, tr("Undo")},
        {PenButtonAction::Redo, tr("Redo")},
        {PenButtonAction::ToggleEraser, tr("Toggle eraser")},
        {PenButtonAction::IncreaseBrushSize, tr("Increase brush size")},
        {PenButtonAction::DecreaseBrushSize, tr("Decrease brush size")},
    }};
    for (const auto& [action, label] : entries) {
      combo->addItem(label, static_cast<int>(action));
    }
    const auto index = combo->findData(static_cast<int>(current));
    combo->setCurrentIndex(index >= 0 ? index : 0);
  };
  auto* pen_primary_button_combo = new QComboBox(pen_group);
  pen_primary_button_combo->setObjectName(QStringLiteral("preferencesPenPrimaryButtonCombo"));
  populate_pen_button_combo(pen_primary_button_combo, pen_input_settings_.primary_button_action);
  auto* pen_secondary_button_combo = new QComboBox(pen_group);
  pen_secondary_button_combo->setObjectName(QStringLiteral("preferencesPenSecondaryButtonCombo"));
  populate_pen_button_combo(pen_secondary_button_combo, pen_input_settings_.secondary_button_action);
  auto* pen_tilt_shape_check = new QCheckBox(tr("Tilt shapes brush dabs"), pen_group);
  pen_tilt_shape_check->setObjectName(QStringLiteral("preferencesPenTiltShapeCheck"));
  pen_tilt_shape_check->setChecked(pen_input_settings_.tilt_shape);
  auto* pen_tilt_roundness_spin = new QSpinBox(pen_group);
  pen_tilt_roundness_spin->setObjectName(QStringLiteral("preferencesPenTiltMinRoundnessSpin"));
  pen_tilt_roundness_spin->setRange(1, 100);
  pen_tilt_roundness_spin->setSuffix(QStringLiteral("%"));
  pen_tilt_roundness_spin->setValue(pen_input_settings_.tilt_min_roundness_percent);

  const auto refresh_pen_controls = [=] {
    const auto pen_enabled = pen_enabled_check->isChecked();
    pen_pressure_size_check->setEnabled(pen_enabled);
    pen_pressure_size_min_spin->setEnabled(pen_enabled && pen_pressure_size_check->isChecked());
    pen_pressure_opacity_check->setEnabled(pen_enabled);
    pen_pressure_opacity_min_spin->setEnabled(pen_enabled && pen_pressure_opacity_check->isChecked());
    pen_eraser_check->setEnabled(pen_enabled);
    pen_primary_button_combo->setEnabled(pen_enabled);
    pen_secondary_button_combo->setEnabled(pen_enabled);
    pen_tilt_shape_check->setEnabled(pen_enabled);
    pen_tilt_roundness_spin->setEnabled(pen_enabled && pen_tilt_shape_check->isChecked());
  };
  connect(pen_enabled_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  connect(pen_pressure_size_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  connect(pen_pressure_opacity_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  connect(pen_tilt_shape_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  refresh_pen_controls();

  pen_form->addRow(pen_enabled_check);
  pen_form->addRow(pen_pressure_size_check);
  pen_form->addRow(tr("Minimum size:"), pen_pressure_size_min_spin);
  pen_form->addRow(pen_pressure_opacity_check);
  pen_form->addRow(tr("Minimum opacity:"), pen_pressure_opacity_min_spin);
  auto* pen_pad_hint_label = new QLabel(
      tr("Set the pen buttons to Right Mouse Click and Middle Mouse Click in your tablet driver: while the "
         "pen is over the canvas, a right click triggers the Upper action and a middle click the Lower one. "
         "Buttons set to Scroll or Pan are handled by the driver and cannot trigger these actions. Tablet pad "
         "buttons (express keys) are also driver-only: map them to keyboard shortcuts such as Undo, Redo, "
         "[ and ] for brush size, and E for the eraser."),
      pen_group);
  pen_pad_hint_label->setObjectName(QStringLiteral("preferencesPenPadButtonsHint"));
  pen_pad_hint_label->setWordWrap(true);
  pen_pad_hint_label->setEnabled(false);

  pen_form->addRow(pen_eraser_check);
  pen_form->addRow(pen_wheel_zoom_check);
  pen_form->addRow(tr("Upper pen button:"), pen_primary_button_combo);
  pen_form->addRow(tr("Lower pen button:"), pen_secondary_button_combo);
  pen_form->addRow(pen_pad_hint_label);
  pen_form->addRow(pen_tilt_shape_check);
  pen_form->addRow(tr("Minimum tilt roundness:"), pen_tilt_roundness_spin);
  pen_layout->addWidget(pen_group);
  pen_layout->addStretch(1);
  tabs->addTab(pen_page, tr("Pen"));

  auto [view_page, view_layout] = make_tab_page(tabs);
  auto* view_group = new QFrame(view_page);
  view_group->setObjectName(QStringLiteral("preferencesCanvasAidsGroup"));
  configure_panel(view_group);
  auto* view_form = new QFormLayout(view_group);
  configure_form(view_form);

  auto* ruler_units_combo = new QComboBox(view_group);
  ruler_units_combo->setObjectName(QStringLiteral("preferencesRulerUnitsCombo"));
  for (const auto ruler_unit_option :
       {MeasurementUnit::Pixels, MeasurementUnit::Inches, MeasurementUnit::Centimeters,
        MeasurementUnit::Millimeters, MeasurementUnit::Points, MeasurementUnit::Percent}) {
    ruler_units_combo->addItem(measurement_unit_name(ruler_unit_option),
                               measurement_unit_settings_token(ruler_unit_option));
  }
  const auto ruler_units_index =
      ruler_units_combo->findData(measurement_unit_settings_token(ruler_unit_));
  ruler_units_combo->setCurrentIndex(ruler_units_index >= 0 ? ruler_units_index : 0);

  auto* default_rulers_check = new QCheckBox(tr("Show rulers"), view_group);
  default_rulers_check->setObjectName(QStringLiteral("preferencesShowRulersCheck"));
  default_rulers_check->setChecked(view_rulers_visible_);
  auto* default_grid_check = new QCheckBox(tr("Show grid"), view_group);
  default_grid_check->setObjectName(QStringLiteral("preferencesShowGridCheck"));
  default_grid_check->setChecked(view_grid_visible_);
  auto* default_guides_check = new QCheckBox(tr("Show guides"), view_group);
  default_guides_check->setObjectName(QStringLiteral("preferencesShowGuidesCheck"));
  default_guides_check->setChecked(view_guides_visible_);
  auto* lock_guides_check = new QCheckBox(tr("Lock guides"), view_group);
  lock_guides_check->setObjectName(QStringLiteral("preferencesLockGuidesCheck"));
  lock_guides_check->setChecked(view_guides_locked_);
  auto* snap_check = new QCheckBox(tr("Enable snapping"), view_group);
  snap_check->setObjectName(QStringLiteral("preferencesSnapCheck"));
  snap_check->setChecked(view_snap_enabled_);

  auto* grid_spacing_spin = new QDoubleSpinBox(view_group);
  grid_spacing_spin->setObjectName(QStringLiteral("preferencesGridSpacingSpin"));
  grid_spacing_spin->setRange(0.03125, 10000.0);
  grid_spacing_spin->setDecimals(3);
  grid_spacing_spin->setSuffix(tr(" px"));
  grid_spacing_spin->setValue(static_cast<double>(view_grid_spacing_32_) / 32.0);
  auto* grid_subdivisions_spin = new QSpinBox(view_group);
  grid_subdivisions_spin->setObjectName(QStringLiteral("preferencesGridSubdivisionsSpin"));
  grid_subdivisions_spin->setRange(1, 64);
  grid_subdivisions_spin->setValue(view_grid_subdivisions_);
  auto* grid_style_combo = new QComboBox(view_group);
  grid_style_combo->setObjectName(QStringLiteral("preferencesGridStyleCombo"));
  grid_style_combo->addItem(tr("Lines"), 0);
  grid_style_combo->addItem(tr("Dots"), 1);
  grid_style_combo->setCurrentIndex(std::clamp(view_grid_style_, 0, 1));

  auto selected_grid_color = view_grid_color_;
  auto selected_guide_color = view_guide_color_;
  auto* grid_color_button = new QPushButton(view_group);
  grid_color_button->setObjectName(QStringLiteral("preferencesGridColorButton"));
  auto* guide_color_button = new QPushButton(view_group);
  guide_color_button->setObjectName(QStringLiteral("preferencesGuideColorButton"));
  const auto refresh_color_choice_button = [](QPushButton* button, QColor color) {
    button->setText(overlay_color_summary_text(color));
    button->setIcon(overlay_color_swatch_icon(color));
    button->setIconSize(QSize(48, 24));
    button->setToolTip(overlay_color_summary_text(color));
    button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
  };
  auto* overlay_preview = new QLabel(view_group);
  overlay_preview->setObjectName(QStringLiteral("preferencesGridOverlayPreview"));
  overlay_preview->setAlignment(Qt::AlignCenter);
  overlay_preview->setFixedSize(218, 86);
  const auto refresh_overlay_preview = [&] {
    overlay_preview->setPixmap(grid_overlay_preview_pixmap(selected_grid_color, selected_guide_color,
                                                           grid_style_combo->currentData().toInt(),
                                                           grid_subdivisions_spin->value()));
  };
  refresh_color_choice_button(grid_color_button, selected_grid_color);
  refresh_color_choice_button(guide_color_button, selected_guide_color);
  refresh_overlay_preview();
  connect(grid_color_button, &QPushButton::clicked, &dialog, [&] {
    const auto color = QColorDialog::getColor(selected_grid_color, &dialog, tr("Grid Color"),
                                              QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
      selected_grid_color = color;
      refresh_color_choice_button(grid_color_button, selected_grid_color);
      refresh_overlay_preview();
    }
  });
  connect(guide_color_button, &QPushButton::clicked, &dialog, [&] {
    const auto color = QColorDialog::getColor(selected_guide_color, &dialog, tr("Guide Color"),
                                              QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
      selected_guide_color = color;
      refresh_color_choice_button(guide_color_button, selected_guide_color);
      refresh_overlay_preview();
    }
  });
  connect(grid_subdivisions_spin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
          [&](int) { refresh_overlay_preview(); });
  connect(grid_style_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
          [&](int) { refresh_overlay_preview(); });

  auto* snap_guides_check = new QCheckBox(tr("Guides"), &dialog);
  snap_guides_check->setObjectName(QStringLiteral("preferencesSnapGuidesCheck"));
  snap_guides_check->setChecked(view_snap_to_guides_);
  auto* snap_grid_check = new QCheckBox(tr("Grid"), &dialog);
  snap_grid_check->setObjectName(QStringLiteral("preferencesSnapGridCheck"));
  snap_grid_check->setChecked(view_snap_to_grid_);
  auto* snap_document_check = new QCheckBox(tr("Document bounds and center"), &dialog);
  snap_document_check->setObjectName(QStringLiteral("preferencesSnapDocumentCheck"));
  snap_document_check->setChecked(view_snap_to_document_);
  auto* snap_layers_check = new QCheckBox(tr("Layer bounds and centers"), &dialog);
  snap_layers_check->setObjectName(QStringLiteral("preferencesSnapLayersCheck"));
  snap_layers_check->setChecked(view_snap_to_layers_);
  auto* snap_selection_check = new QCheckBox(tr("Selection bounds and center"), &dialog);
  snap_selection_check->setObjectName(QStringLiteral("preferencesSnapSelectionCheck"));
  snap_selection_check->setChecked(view_snap_to_selection_);

  auto* visibility_row = new QWidget(view_group);
  visibility_row->setObjectName(QStringLiteral("preferencesVisibilityRow"));
  auto* visibility_layout = new QGridLayout(visibility_row);
  visibility_layout->setContentsMargins(0, 0, 0, 0);
  visibility_layout->setHorizontalSpacing(18);
  visibility_layout->setVerticalSpacing(6);
  visibility_layout->addWidget(default_rulers_check, 0, 0);
  visibility_layout->addWidget(default_grid_check, 0, 1);
  visibility_layout->addWidget(default_guides_check, 1, 0);
  visibility_layout->addWidget(lock_guides_check, 1, 1);
  auto* snap_targets_row = new QWidget(&dialog);
  snap_targets_row->setObjectName(QStringLiteral("preferencesSnapTargetsRow"));
  auto* snap_targets_layout = new QGridLayout(snap_targets_row);
  snap_targets_layout->setContentsMargins(0, 0, 0, 0);
  snap_targets_layout->setHorizontalSpacing(18);
  snap_targets_layout->setVerticalSpacing(6);
  snap_targets_layout->addWidget(snap_guides_check, 0, 0);
  snap_targets_layout->addWidget(snap_grid_check, 0, 1);
  snap_targets_layout->addWidget(snap_document_check, 1, 0, 1, 2);
  snap_targets_layout->addWidget(snap_layers_check, 2, 0, 1, 2);
  snap_targets_layout->addWidget(snap_selection_check, 3, 0, 1, 2);

  view_form->addRow(tr("Ruler units:"), ruler_units_combo);
  view_form->addRow(tr("Default visibility:"), visibility_row);
  view_form->addRow(tr("Grid spacing:"), grid_spacing_spin);
  view_form->addRow(tr("Grid subdivisions:"), grid_subdivisions_spin);
  view_form->addRow(tr("Grid style:"), grid_style_combo);
  view_form->addRow(tr("Grid color:"), grid_color_button);
  view_form->addRow(tr("Guide color:"), guide_color_button);
  view_form->addRow(tr("Overlay preview:"), overlay_preview);
  view_layout->addWidget(view_group);
  view_layout->addStretch(1);
  tabs->addTab(view_page, tr("Grid and Guides"));

  auto [snapping_page, snapping_layout] = make_tab_page(tabs);
  auto* snapping_group = new QFrame(snapping_page);
  snapping_group->setObjectName(QStringLiteral("preferencesSnappingGroup"));
  configure_panel(snapping_group);
  auto* snapping_form = new QFormLayout(snapping_group);
  configure_form(snapping_form);
  snapping_form->addRow(tr("Snap:"), snap_check);
  snapping_form->addRow(tr("Snap targets:"), snap_targets_row);
  snapping_layout->addWidget(snapping_group);
  snapping_layout->addStretch(1);
  tabs->addTab(snapping_page, tr("Snapping"));

  auto [hotkeys_page, hotkeys_layout] = make_tab_page(tabs);
  auto* hotkey_editor = new HotkeyEditorPanel(hotkey_registry_, menuBar(), hotkeys_page);
  hotkeys_layout->addWidget(hotkey_editor);
  hotkeys_layout->addStretch(1);
  tabs->addTab(hotkeys_page, tr("Hotkeys"));

  content->addWidget(tabs, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  buttons->setObjectName(QStringLiteral("preferencesButtonBox"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  content->addWidget(buttons);

  // Applied after every child widget exists: Qt does not reliably pick up
  // sub-control rules (QSpinBox::up-button) for widgets created on hidden
  // tab pages after the stylesheet was set.
  dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(R"(
    QDialog#patchyPreferencesDialog QTabWidget::pane {
      border: 1px solid #444444;
      background: #2b2b2b;
      top: -1px;
    }
    QDialog#patchyPreferencesDialog QTabBar::tab {
      background: #2b2b2b;
      border: 1px solid #444444;
      border-bottom-color: #2b2b2b;
      color: #dcdcdc;
      padding: 7px 18px;
      min-width: 92px;
    }
    QDialog#patchyPreferencesDialog QTabBar::tab:hover:!selected {
      background: #343434;
    }
    QDialog#patchyPreferencesDialog QTabBar::tab:selected {
      background: #383838;
      color: #ffffff;
      border-bottom-color: #383838;
    }
    QDialog#patchyPreferencesDialog QFrame[preferencesPanel="true"] {
      background: #303030;
      border: 1px solid #464646;
      border-radius: 4px;
    }
    QDialog#patchyPreferencesDialog QPushButton#preferencesGridColorButton,
    QDialog#patchyPreferencesDialog QPushButton#preferencesGuideColorButton {
      background: #3a3a3a;
      border: 1px solid #626262;
      border-radius: 3px;
      color: #f0f0f0;
      min-height: 30px;
      min-width: 158px;
      padding: 3px 9px;
      text-align: left;
    }
    QDialog#patchyPreferencesDialog QPushButton#preferencesGridColorButton:hover,
    QDialog#patchyPreferencesDialog QPushButton#preferencesGuideColorButton:hover {
      border-color: #80bfff;
      background: #404040;
    }
    QDialog#patchyPreferencesDialog QLabel#preferencesGridOverlayPreview {
      background: #202020;
      border: 1px solid #575757;
      padding: 0;
    }
    QDialog#patchyPreferencesDialog QScrollArea#preferencesTabScroll,
    QDialog#patchyPreferencesDialog QWidget#preferencesTabPage {
      background: transparent;
    }
    /* Plain-QWidget field containers inherit the global QWidget background, which
       shows as a mismatched band against the #303030 panels. */
    QDialog#patchyPreferencesDialog QWidget#preferencesVisibilityRow,
    QDialog#patchyPreferencesDialog QWidget#preferencesSnapTargetsRow {
      background: transparent;
    }
  )") + dialog_spinbox_button_style());

  if (exec_dialog(dialog) == QDialog::Accepted) {
    hotkey_editor->commit();
    const auto new_grid_spacing_32 =
        std::clamp(static_cast<int>(std::lround(grid_spacing_spin->value() * 32.0)), 1, 320000);
    settings.setValue(QStringLiteral("updates/checkOnStartup"), update_check->isChecked());
    settings.setValue(QStringLiteral("imports/showPsdWarningsAndInfo"), psd_import_warnings_check->isChecked());
    settings.setValue(QStringLiteral("imports/showRawDevelopDialog"), raw_develop_check->isChecked());
    settings.setValue(QStringLiteral("imports/adoptIndexedPalette"), indexed_open_combo->currentData().toString());
    const int selected_gui_scale = gui_scale_combo->currentData().toInt();
    const int previous_gui_scale =
        settings.value(QStringLiteral("preferences/guiScalePercent"), 100).toInt();
    if (selected_gui_scale != previous_gui_scale) {
      settings.setValue(QStringLiteral("preferences/guiScalePercent"), selected_gui_scale);
      show_information_message(this, tr("Interface Scale"),
                               tr("Restart Patchy for the new interface scale to take effect."),
                               QStringLiteral("preferencesInterfaceScaleMessageBox"));
    }
    set_ruler_unit_preference(measurement_unit_from_settings_token(
        ruler_units_combo->currentData().toString(), MeasurementUnit::Pixels));
    settings.setValue(QStringLiteral("view/rulerUnits"), ruler_units_combo->currentData().toString());
    pen_input_settings_.enabled = pen_enabled_check->isChecked();
    pen_input_settings_.pressure_size = pen_pressure_size_check->isChecked();
    pen_input_settings_.pressure_size_min_percent = pen_pressure_size_min_spin->value();
    pen_input_settings_.pressure_opacity = pen_pressure_opacity_check->isChecked();
    pen_input_settings_.pressure_opacity_min_percent = pen_pressure_opacity_min_spin->value();
    pen_input_settings_.use_eraser_tip = pen_eraser_check->isChecked();
    pen_input_settings_.primary_button_action =
        static_cast<PenButtonAction>(pen_primary_button_combo->currentData().toInt());
    pen_input_settings_.secondary_button_action =
        static_cast<PenButtonAction>(pen_secondary_button_combo->currentData().toInt());
    pen_input_settings_.tilt_shape = pen_tilt_shape_check->isChecked();
    pen_input_settings_.tilt_min_roundness_percent = pen_tilt_roundness_spin->value();
    wheel_zooms_ = pen_wheel_zoom_check->isChecked();
    view_rulers_visible_ = default_rulers_check->isChecked();
    view_grid_visible_ = default_grid_check->isChecked();
    view_guides_visible_ = default_guides_check->isChecked();
    view_guides_locked_ = lock_guides_check->isChecked();
    view_snap_enabled_ = snap_check->isChecked();
    view_snap_to_guides_ = snap_guides_check->isChecked();
    view_snap_to_grid_ = snap_grid_check->isChecked();
    view_snap_to_document_ = snap_document_check->isChecked();
    view_snap_to_layers_ = snap_layers_check->isChecked();
    view_snap_to_selection_ = snap_selection_check->isChecked();
    view_grid_spacing_32_ = new_grid_spacing_32;
    view_grid_subdivisions_ = grid_subdivisions_spin->value();
    view_grid_style_ = grid_style_combo->currentData().toInt();
    view_grid_color_ = selected_grid_color;
    view_guide_color_ = selected_guide_color;
    if (canvas_ != nullptr && (document().grid_settings().horizontal_cycle_32 != new_grid_spacing_32 ||
                               document().grid_settings().vertical_cycle_32 != new_grid_spacing_32)) {
      push_undo_snapshot(tr("Grid Preferences"));
      document().grid_settings().horizontal_cycle_32 = new_grid_spacing_32;
      document().grid_settings().vertical_cycle_32 = new_grid_spacing_32;
      canvas_->document_changed();
    }
    if (view_rulers_action_ != nullptr) {
      view_rulers_action_->setChecked(view_rulers_visible_);
    }
    if (view_grid_action_ != nullptr) {
      view_grid_action_->setChecked(view_grid_visible_);
    }
    if (view_guides_action_ != nullptr) {
      view_guides_action_->setChecked(view_guides_visible_);
    }
    if (view_snap_action_ != nullptr) {
      view_snap_action_->setChecked(view_snap_enabled_);
    }
    if (view_lock_guides_action_ != nullptr) {
      view_lock_guides_action_->setChecked(view_guides_locked_);
    }
    if (view_snap_guides_action_ != nullptr) {
      view_snap_guides_action_->setChecked(view_snap_to_guides_);
    }
    if (view_snap_grid_action_ != nullptr) {
      view_snap_grid_action_->setChecked(view_snap_to_grid_);
    }
    if (view_snap_document_action_ != nullptr) {
      view_snap_document_action_->setChecked(view_snap_to_document_);
    }
    if (view_snap_layers_action_ != nullptr) {
      view_snap_layers_action_->setChecked(view_snap_to_layers_);
    }
    if (view_snap_selection_action_ != nullptr) {
      view_snap_selection_action_->setChecked(view_snap_to_selection_);
    }
    for (const auto& active_session : sessions_) {
      apply_canvas_aid_settings(active_session->canvas);
      apply_pen_input_settings(active_session->canvas);
    }
    save_pen_input_settings();
    save_view_settings();
  }
  if (pending_stress_preset.has_value()) {
    run_stress_test_interactive(*pending_stress_preset);
  }
}

void MainWindow::new_guide_dialog() {
  if (canvas_ == nullptr) {
    return;
  }

  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("newGuideDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, tr("New Guide"));
  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(8);

  auto* orientation_combo = new QComboBox(&dialog);
  orientation_combo->setObjectName(QStringLiteral("newGuideOrientationCombo"));
  orientation_combo->addItem(tr("Vertical"), static_cast<int>(GuideOrientation::Vertical));
  orientation_combo->addItem(tr("Horizontal"), static_cast<int>(GuideOrientation::Horizontal));
  auto* position_spin = new QDoubleSpinBox(&dialog);
  position_spin->setObjectName(QStringLiteral("newGuidePositionSpin"));
  position_spin->setRange(0.0, std::max(document().width(), document().height()));
  position_spin->setDecimals(3);
  position_spin->setSuffix(tr(" px"));
  position_spin->setValue(0.0);
  form->addRow(tr("Orientation:"), orientation_combo);
  form->addRow(tr("Position:"), position_spin);
  content->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("newGuideButtonBox"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  content->addWidget(buttons);

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return;
  }

  const auto orientation = static_cast<GuideOrientation>(orientation_combo->currentData().toInt());
  canvas_->add_guide(orientation, static_cast<std::int32_t>(std::lround(position_spin->value() * 32.0)));
}

void MainWindow::new_guide_layout_dialog() {
  if (canvas_ == nullptr) {
    return;
  }

  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("newGuideLayoutDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, tr("New Guide Layout"));
  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(8);

  auto* columns_spin = new QSpinBox(&dialog);
  columns_spin->setObjectName(QStringLiteral("newGuideLayoutColumnsSpin"));
  columns_spin->setRange(0, 64);
  columns_spin->setValue(2);
  auto* rows_spin = new QSpinBox(&dialog);
  rows_spin->setObjectName(QStringLiteral("newGuideLayoutRowsSpin"));
  rows_spin->setRange(0, 64);
  rows_spin->setValue(2);
  auto* clear_existing = new QCheckBox(tr("Clear existing guides"), &dialog);
  clear_existing->setObjectName(QStringLiteral("newGuideLayoutClearExistingCheck"));
  clear_existing->setChecked(false);
  form->addRow(tr("Columns:"), columns_spin);
  form->addRow(tr("Rows:"), rows_spin);
  form->addRow(QString(), clear_existing);
  content->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("newGuideLayoutButtonBox"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  content->addWidget(buttons);

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return;
  }

  if (clear_existing->isChecked() && !document().guides().empty()) {
    push_undo_snapshot(tr("New Guide Layout"));
    document().guides().clear();
  } else {
    push_undo_snapshot(tr("New Guide Layout"));
  }
  const auto add_even_guides = [this](GuideOrientation orientation, int count, int span) {
    if (count <= 0 || span <= 0) {
      return;
    }
    for (int index = 1; index < count; ++index) {
      const auto position = static_cast<double>(span) * static_cast<double>(index) / static_cast<double>(count);
      document().guides().push_back(DocumentGuide{orientation, static_cast<std::int32_t>(std::lround(position * 32.0))});
    }
  };
  add_even_guides(GuideOrientation::Vertical, columns_spin->value(), document().width());
  add_even_guides(GuideOrientation::Horizontal, rows_spin->value(), document().height());
  canvas_->document_changed();
}

void MainWindow::clear_guides() {
  if (canvas_ == nullptr) {
    return;
  }
  canvas_->clear_guides();
}

void MainWindow::clear_selected_guides() {
  if (canvas_ == nullptr) {
    return;
  }
  canvas_->clear_selected_guides();
}

void MainWindow::set_ruler_unit_preference(MeasurementUnit unit) {
  if (ruler_unit_ == unit) {
    return;
  }
  ruler_unit_ = unit;
  for (const auto& active_session : sessions_) {
    apply_canvas_aid_settings(active_session->canvas);
  }
  save_view_settings();
  refresh_document_info();
}

void MainWindow::apply_canvas_aid_settings(CanvasWidget* canvas) const {
  if (canvas == nullptr) {
    return;
  }
  canvas->set_rulers_visible(view_rulers_visible_);
  canvas->set_ruler_unit(ruler_unit_);
  canvas->set_grid_visible(view_grid_visible_);
  canvas->set_guides_visible(view_guides_visible_);
  canvas->set_guides_locked(view_guides_locked_);
  canvas->set_snap_enabled(view_snap_enabled_);
  canvas->set_snap_to_guides(view_snap_to_guides_);
  canvas->set_snap_to_grid(view_snap_to_grid_);
  canvas->set_snap_to_document(view_snap_to_document_);
  canvas->set_snap_to_layers(view_snap_to_layers_);
  canvas->set_snap_to_selection(view_snap_to_selection_);
  canvas->set_grid_subdivisions(view_grid_subdivisions_);
  canvas->set_grid_style(view_grid_style_);
  canvas->set_grid_color(view_grid_color_);
  canvas->set_guide_color(view_guide_color_);
  canvas->set_target_path_visible(view_target_path_visible_);
}

void MainWindow::apply_pen_input_settings(CanvasWidget* canvas) const {
  if (canvas == nullptr) {
    return;
  }
  canvas->set_pen_input_settings(pen_input_settings_);
  canvas->set_wheel_zooms(wheel_zooms_);
}

void MainWindow::handle_pen_button_action(PenButtonAction action) {
  switch (action) {
    case PenButtonAction::Undo:
      undo();
      break;
    case PenButtonAction::Redo:
      redo();
      break;
    case PenButtonAction::ToggleEraser:
      if (current_tool_ == CanvasTool::Eraser) {
        activate_tool(tool_before_eraser_toggle_);
      } else {
        tool_before_eraser_toggle_ = current_tool_;
        activate_tool(CanvasTool::Eraser);
      }
      break;
    case PenButtonAction::IncreaseBrushSize:
    case PenButtonAction::DecreaseBrushSize: {
      if (auto* brush_size = findChild<QSpinBox*>(QStringLiteral("brushSizeSpin")); brush_size != nullptr) {
        const int direction = action == PenButtonAction::IncreaseBrushSize ? 1 : -1;
        const int value = brush_size->value();
        brush_size->setValue(value + direction * proportional_brush_step(value, direction, false));
      }
      break;
    }
    case PenButtonAction::SwapColors:
      if (canvas_ != nullptr) {
        const auto primary = canvas_->primary_color();
        canvas_->set_primary_color(canvas_->secondary_color());
        canvas_->set_secondary_color(primary);
        refresh_color_buttons();
      }
      break;
    case PenButtonAction::PanCanvas:
    case PenButtonAction::ZoomCanvas:
    case PenButtonAction::PickColor:
    case PenButtonAction::SetCloneSource:
    case PenButtonAction::None:
      break;
  }
}

void MainWindow::load_pen_input_settings() {
  auto settings = app_settings();
  pen_input_settings_.enabled = settings.value(QStringLiteral("input/pen/enabled"), true).toBool();
  pen_input_settings_.pressure_size = settings.value(QStringLiteral("input/pen/pressureSize"), true).toBool();
  pen_input_settings_.pressure_size_min_percent =
      std::clamp(settings.value(QStringLiteral("input/pen/pressureSizeMinPercent"), 20).toInt(), 1, 100);
  pen_input_settings_.pressure_opacity =
      settings.value(QStringLiteral("input/pen/pressureOpacity"), true).toBool();
  pen_input_settings_.pressure_opacity_min_percent =
      std::clamp(settings.value(QStringLiteral("input/pen/pressureOpacityMinPercent"), 15).toInt(), 1, 100);
  pen_input_settings_.use_eraser_tip = settings.value(QStringLiteral("input/pen/useEraserTip"), true).toBool();
  auto primary_token = settings.value(QStringLiteral("input/pen/primaryButtonAction")).toString();
  auto secondary_token = settings.value(QStringLiteral("input/pen/secondaryButtonAction")).toString();
  if (primary_token.isEmpty()) {
    // Migrate from the legacy "barrel button pans canvas" toggle.
    const auto legacy_pans = settings.value(QStringLiteral("input/pen/barrelButtonPans"), true).toBool();
    primary_token =
        pen_button_action_to_token(legacy_pans ? PenButtonAction::PanCanvas : PenButtonAction::None);
  }
  if (secondary_token.isEmpty()) {
    secondary_token = pen_button_action_to_token(PenButtonAction::PickColor);
  }
  pen_input_settings_.primary_button_action = pen_button_action_from_token(primary_token);
  pen_input_settings_.secondary_button_action = pen_button_action_from_token(secondary_token);
  pen_input_settings_.tilt_shape = settings.value(QStringLiteral("input/pen/tiltShape"), false).toBool();
  pen_input_settings_.tilt_min_roundness_percent =
      std::clamp(settings.value(QStringLiteral("input/pen/tiltMinRoundnessPercent"), 35).toInt(), 1, 100);
  wheel_zooms_ = settings.value(QStringLiteral("input/wheelZooms"), kWheelZoomsDefault).toBool();
  apply_pen_input_settings(canvas_);
}

void MainWindow::save_pen_input_settings() const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("input/pen/enabled"), pen_input_settings_.enabled);
  settings.setValue(QStringLiteral("input/pen/pressureSize"), pen_input_settings_.pressure_size);
  settings.setValue(QStringLiteral("input/pen/pressureSizeMinPercent"),
                    pen_input_settings_.pressure_size_min_percent);
  settings.setValue(QStringLiteral("input/pen/pressureOpacity"), pen_input_settings_.pressure_opacity);
  settings.setValue(QStringLiteral("input/pen/pressureOpacityMinPercent"),
                    pen_input_settings_.pressure_opacity_min_percent);
  settings.setValue(QStringLiteral("input/pen/useEraserTip"), pen_input_settings_.use_eraser_tip);
  settings.setValue(QStringLiteral("input/pen/primaryButtonAction"),
                    pen_button_action_to_token(pen_input_settings_.primary_button_action));
  settings.setValue(QStringLiteral("input/pen/secondaryButtonAction"),
                    pen_button_action_to_token(pen_input_settings_.secondary_button_action));
  settings.setValue(QStringLiteral("input/pen/tiltShape"), pen_input_settings_.tilt_shape);
  settings.setValue(QStringLiteral("input/pen/tiltMinRoundnessPercent"),
                    pen_input_settings_.tilt_min_roundness_percent);
  settings.setValue(QStringLiteral("input/wheelZooms"), wheel_zooms_);
}

void MainWindow::load_view_settings() {
  auto settings = app_settings();
  view_rulers_visible_ = settings.value(QStringLiteral("view/rulersVisible"), view_rulers_visible_).toBool();
  ruler_unit_ = measurement_unit_from_settings_token(
      settings.value(QStringLiteral("view/rulerUnits"), QStringLiteral("px")).toString(),
      MeasurementUnit::Pixels);
  view_grid_visible_ = settings.value(QStringLiteral("view/gridVisible"), view_grid_visible_).toBool();
  view_guides_visible_ = settings.value(QStringLiteral("view/guidesVisible"), view_guides_visible_).toBool();
  view_guides_locked_ = settings.value(QStringLiteral("view/guidesLocked"), view_guides_locked_).toBool();
  view_snap_enabled_ = settings.value(QStringLiteral("view/snapEnabled"), view_snap_enabled_).toBool();
  view_snap_to_guides_ = settings.value(QStringLiteral("view/snapToGuides"), view_snap_to_guides_).toBool();
  view_snap_to_grid_ = settings.value(QStringLiteral("view/snapToGrid"), view_snap_to_grid_).toBool();
  view_snap_to_document_ = settings.value(QStringLiteral("view/snapToDocument"), view_snap_to_document_).toBool();
  view_snap_to_layers_ = settings.value(QStringLiteral("view/snapToLayers"), view_snap_to_layers_).toBool();
  view_snap_to_selection_ = settings.value(QStringLiteral("view/snapToSelection"), view_snap_to_selection_).toBool();
  view_grid_spacing_32_ = std::clamp(settings.value(QStringLiteral("view/gridSpacing32"), view_grid_spacing_32_).toInt(),
                                     1, 320000);
  view_grid_subdivisions_ =
      std::clamp(settings.value(QStringLiteral("view/gridSubdivisions"), view_grid_subdivisions_).toInt(), 1, 64);
  view_grid_style_ = std::clamp(settings.value(QStringLiteral("view/gridStyle"), view_grid_style_).toInt(), 0, 1);
  view_grid_color_ =
      settings.value(QStringLiteral("view/gridColor"), view_grid_color_).value<QColor>();
  view_guide_color_ =
      settings.value(QStringLiteral("view/guideColor"), view_guide_color_).value<QColor>();
  const bool migrate_legacy_guide_default =
      !settings.value(QStringLiteral("view/guideColorDefaultMigrated"), false).toBool();
  if (!view_grid_color_.isValid()) {
    view_grid_color_ = QColor(78, 154, 255, 105);
  }
  if (!view_guide_color_.isValid() ||
      (migrate_legacy_guide_default && view_guide_color_ == QColor(72, 186, 255, 210))) {
    view_guide_color_ = QColor(255, 70, 180, 230);
  }
  if (migrate_legacy_guide_default) {
    settings.setValue(QStringLiteral("view/guideColorDefaultMigrated"), true);
  }

  if (view_rulers_action_ != nullptr) {
    view_rulers_action_->setChecked(view_rulers_visible_);
  }
  if (view_grid_action_ != nullptr) {
    view_grid_action_->setChecked(view_grid_visible_);
  }
  if (view_guides_action_ != nullptr) {
    view_guides_action_->setChecked(view_guides_visible_);
  }
  if (view_snap_action_ != nullptr) {
    view_snap_action_->setChecked(view_snap_enabled_);
  }
  if (view_lock_guides_action_ != nullptr) {
    view_lock_guides_action_->setChecked(view_guides_locked_);
  }
  if (view_snap_guides_action_ != nullptr) {
    view_snap_guides_action_->setChecked(view_snap_to_guides_);
  }
  if (view_snap_grid_action_ != nullptr) {
    view_snap_grid_action_->setChecked(view_snap_to_grid_);
  }
  if (view_snap_document_action_ != nullptr) {
    view_snap_document_action_->setChecked(view_snap_to_document_);
  }
  if (view_snap_layers_action_ != nullptr) {
    view_snap_layers_action_->setChecked(view_snap_to_layers_);
  }
  if (view_snap_selection_action_ != nullptr) {
    view_snap_selection_action_->setChecked(view_snap_to_selection_);
  }
  apply_canvas_aid_settings(canvas_);
}

void MainWindow::save_view_settings() const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("view/rulersVisible"), view_rulers_visible_);
  settings.setValue(QStringLiteral("view/rulerUnits"), measurement_unit_settings_token(ruler_unit_));
  settings.setValue(QStringLiteral("view/gridVisible"), view_grid_visible_);
  settings.setValue(QStringLiteral("view/guidesVisible"), view_guides_visible_);
  settings.setValue(QStringLiteral("view/guidesLocked"), view_guides_locked_);
  settings.setValue(QStringLiteral("view/snapEnabled"), view_snap_enabled_);
  settings.setValue(QStringLiteral("view/snapToGuides"), view_snap_to_guides_);
  settings.setValue(QStringLiteral("view/snapToGrid"), view_snap_to_grid_);
  settings.setValue(QStringLiteral("view/snapToDocument"), view_snap_to_document_);
  settings.setValue(QStringLiteral("view/snapToLayers"), view_snap_to_layers_);
  settings.setValue(QStringLiteral("view/snapToSelection"), view_snap_to_selection_);
  settings.setValue(QStringLiteral("view/gridSpacing32"), view_grid_spacing_32_);
  settings.setValue(QStringLiteral("view/gridSubdivisions"), view_grid_subdivisions_);
  settings.setValue(QStringLiteral("view/gridStyle"), view_grid_style_);
  settings.setValue(QStringLiteral("view/gridColor"), view_grid_color_);
  settings.setValue(QStringLiteral("view/guideColor"), view_guide_color_);
  settings.setValue(QStringLiteral("view/guideColorDefaultMigrated"), true);
}

}  // namespace patchy::ui
