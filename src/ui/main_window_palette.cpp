// MainWindow palette-mode (indexed color) implementation, split out of main_window.cpp:
// document palette mutations, indexed<->RGB conversion, palette file I/O, panel/status-chip
// refresh, and the advisory compliance scan. Pure function moves; behavior must stay
// identical to the pre-split code. See the "Palette / indexed-color editing mode" notes
// in AGENTS.md before changing anything here.

#include "ui/main_window.hpp"

#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/bmp_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
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
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/print_dialog.hpp"
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
#include <QButtonGroup>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
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

// Recursion helpers for palette-mode document walks: groups recurse, adjustment
// layers have no pixels of their own, everything else with a color buffer counts.
void apply_palette_to_layers(std::vector<Layer>& layers, const patchy::PaletteLut& lut,
                             patchy::PaletteDither dither, std::uint8_t alpha_threshold) {
  for (auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      apply_palette_to_layers(layer.children(), lut, dither, alpha_threshold);
      continue;
    }
    if (layer.kind() == LayerKind::Adjustment) {
      continue;
    }
    auto& pixels = layer.pixels();
    if (pixels.width() <= 0 || pixels.height() <= 0 || pixels.format().channels < 3) {
      continue;
    }
    (void)patchy::apply_palette_to_pixels(pixels, lut, dither, alpha_threshold);
  }
}

[[nodiscard]] bool layers_are_palette_clean(const std::vector<Layer>& layers, const patchy::PaletteLut& lut) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      if (!layers_are_palette_clean(layer.children(), lut)) {
        return false;
      }
      continue;
    }
    if (layer.kind() == LayerKind::Adjustment) {
      continue;
    }
    const auto& pixels = layer.pixels();
    if (pixels.width() <= 0 || pixels.height() <= 0 || pixels.format().channels < 3) {
      continue;
    }
    if (!patchy::pixels_are_palette_clean(pixels, lut)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] RgbColor rgb_from_qcolor(const QColor& color) noexcept {
  return RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                  static_cast<std::uint8_t>(color.blue())};
}

}  // namespace
std::uint64_t MainWindow::next_palette_revision() noexcept {
  // App-globally unique, never reused: undo snapshots may restore any historical
  // (revision, palette) pair, and the canvas LUT cache treats equal revisions as
  // identical palettes.
  static std::atomic<std::uint64_t> counter{1};
  return counter.fetch_add(1, std::memory_order_relaxed);
}

std::vector<RgbColor> MainWindow::displayed_palette_colors() {
  if (!has_active_document()) {
    return {};
  }
  const auto& doc = document();
  if (doc.palette_editing().has_value()) {
    return doc.palette_editing()->palette.colors;
  }
  if (doc.indexed_palette().has_value()) {
    return doc.indexed_palette()->colors;
  }
  return {};
}

void MainWindow::set_document_palette(std::vector<RgbColor> colors, const QString& undo_label,
                                      const QString& status_message) {
  if (!has_active_document() || colors.empty()) {
    return;
  }
  if (colors.size() > 256) {
    colors.resize(256);
  }
  push_undo_snapshot(undo_label);
  auto& doc = document();
  if (doc.palette_editing().has_value()) {
    doc.palette_editing()->palette.colors = std::move(colors);
    doc.palette_editing()->palette_revision = next_palette_revision();
    patchy::sync_document_indexed_palette(doc);
    // Replacing the palette under existing art does not repaint pixels; the
    // advisory compliance scan surfaces any colors that fell outside, and the
    // display re-quantizes against the new palette.
    if (canvas_ != nullptr) {
      canvas_->document_changed();
    }
    schedule_palette_compliance_check();
  } else {
    // No palette mode yet: attach the palette as document metadata only (the
    // Convert command turns it into the editing constraint).
    const auto count = colors.size();
    const std::uint16_t depth = count <= 4 ? 2 : (count <= 16 ? 4 : 8);
    doc.indexed_palette() = DocumentIndexedPalette{std::move(colors), depth};
  }
  refresh_palette_panel();
  statusBar()->showMessage(status_message);
}

void MainWindow::edit_palette_entry(int index) {
  if (!has_active_document()) {
    return;
  }
  const auto colors = displayed_palette_colors();
  if (index < 0 || index >= static_cast<int>(colors.size())) {
    return;
  }
  const auto before = colors[static_cast<std::size_t>(index)];
  const auto mode_active = document().palette_editing().has_value();

  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("paletteEntryDialog"));
  dialog.setWindowTitle(tr("Edit Palette Color"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* picker = new PatchyColorPicker(QColor(before.red, before.green, before.blue), &dialog);
  layout->addWidget(picker);
  auto* remap_check = new QCheckBox(tr("Remap existing pixels"), &dialog);
  remap_check->setObjectName(QStringLiteral("paletteRemapCheck"));
  remap_check->setChecked(true);
  remap_check->setVisible(mode_active);
  layout->addWidget(remap_check);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return;
  }
  const auto after = rgb_from_qcolor(picker->currentColor());
  if (after.red == before.red && after.green == before.green && after.blue == before.blue) {
    return;
  }
  apply_palette_entry_color(index, after, remap_check->isChecked(), tr("Edit palette entry"));
  statusBar()->showMessage(tr("Palette color updated"));
}

void MainWindow::apply_palette_entry_color(int index, RgbColor color, bool remap_pixels,
                                           const QString& undo_label) {
  if (!has_active_document()) {
    return;
  }
  const auto colors = displayed_palette_colors();
  if (index < 0 || index >= static_cast<int>(colors.size())) {
    return;
  }
  const auto before = colors[static_cast<std::size_t>(index)];
  if (before.red == color.red && before.green == color.green && before.blue == color.blue) {
    return;
  }

  push_undo_snapshot(undo_label);
  auto& doc = document();
  if (doc.palette_editing().has_value()) {
    doc.palette_editing()->palette.colors[static_cast<std::size_t>(index)] = color;
    doc.palette_editing()->palette_revision = next_palette_revision();
    patchy::sync_document_indexed_palette(doc);
    if (remap_pixels) {
      // Lossless palette swap: enforced pixels are exact palette colors, so an
      // exact-match recolor moves every use of the old entry to the new one.
      patchy::remap_document_exact_color(doc, before, color);
    }
    if (canvas_ != nullptr) {
      canvas_->document_changed();
    }
    refresh_layer_list();
  } else if (doc.indexed_palette().has_value() &&
             index < static_cast<int>(doc.indexed_palette()->colors.size())) {
    doc.indexed_palette()->colors[static_cast<std::size_t>(index)] = color;
  }
  refresh_palette_panel();
  schedule_palette_compliance_check();
}

void MainWindow::swap_palette_entries(int from_index, int to_index) {
  if (!has_active_document() || from_index == to_index) {
    return;
  }
  auto& doc = document();
  auto* colors = doc.palette_editing().has_value() ? &doc.palette_editing()->palette.colors
                 : doc.indexed_palette().has_value() ? &doc.indexed_palette()->colors
                                                     : nullptr;
  if (colors == nullptr || from_index < 0 || to_index < 0 || from_index >= static_cast<int>(colors->size()) ||
      to_index >= static_cast<int>(colors->size())) {
    return;
  }
  push_undo_snapshot(tr("Swap palette colors"));
  // Pixels reference colors by value, so swapping entries never repaints; it
  // only changes which index each color exports as.
  std::swap((*colors)[static_cast<std::size_t>(from_index)], (*colors)[static_cast<std::size_t>(to_index)]);
  if (doc.palette_editing().has_value()) {
    doc.palette_editing()->palette_revision = next_palette_revision();
    patchy::sync_document_indexed_palette(doc);
  }
  refresh_palette_panel();
  statusBar()->showMessage(tr("Swapped palette indexes %1 and %2").arg(from_index).arg(to_index));
}

void MainWindow::copy_selected_palette_color() {
  if (palette_panel_ == nullptr) {
    return;
  }
  const auto color = palette_panel_->selected_color();
  if (!color.has_value()) {
    return;
  }
  const auto name = QColor(color->red, color->green, color->blue).name();
  QApplication::clipboard()->setText(name);
  statusBar()->showMessage(tr("Copied palette color %1").arg(name));
}

void MainWindow::paste_clipboard_color_to_palette() {
  if (palette_panel_ == nullptr) {
    return;
  }
  const auto index = palette_panel_->selected_index();
  if (index < 0) {
    return;
  }
  auto text = QApplication::clipboard()->text().trimmed();
  if (text.startsWith(QLatin1Char('#'))) {
    text.remove(0, 1);
  }
  bool valid = text.size() == 6;
  for (const auto character : text) {
    valid = valid && isxdigit(character.toLatin1()) != 0;
  }
  if (!valid) {
    statusBar()->showMessage(tr("The clipboard does not contain a color (expected #RRGGBB)"));
    return;
  }
  const QColor color(QStringLiteral("#") + text);
  apply_palette_entry_color(index, rgb_from_qcolor(color), true, tr("Paste palette color"));
  statusBar()->showMessage(tr("Pasted %1 into palette index %2").arg(color.name()).arg(index));
}

void MainWindow::add_palette_entry_from_foreground() {
  if (!has_active_document() || canvas_ == nullptr) {
    return;
  }
  auto colors = displayed_palette_colors();
  const auto color = rgb_from_qcolor(canvas_->primary_color());
  for (const auto& existing : colors) {
    if (existing.red == color.red && existing.green == color.green && existing.blue == color.blue) {
      statusBar()->showMessage(tr("The foreground color is already in the palette"));
      return;
    }
  }
  if (colors.size() >= 256) {
    statusBar()->showMessage(tr("The palette is full (256 colors)"));
    return;
  }
  colors.push_back(color);
  set_document_palette(std::move(colors), tr("Add palette color"), tr("Added the foreground color to the palette"));
}

void MainWindow::remove_palette_entry(int index) {
  if (!has_active_document()) {
    return;
  }
  auto colors = displayed_palette_colors();
  if (index < 0 || index >= static_cast<int>(colors.size())) {
    return;
  }
  if (colors.size() <= 1) {
    statusBar()->showMessage(tr("A palette needs at least one color"));
    return;
  }
  colors.erase(colors.begin() + index);
  set_document_palette(std::move(colors), tr("Remove palette color"), tr("Removed the palette color"));
}

void MainWindow::extract_palette_from_image() {
  if (!has_active_document()) {
    return;
  }
  const auto flattened = Compositor().flatten_rgb8(document());
  const auto exact = patchy::exact_palette_from_pixels(flattened, 256, 0);
  if (!exact.has_value()) {
    statusBar()->showMessage(
        tr("The image has more than 256 colors. Use Image > Mode > Indexed (Palette) to optimize it down."));
    return;
  }
  const auto count = static_cast<int>(exact->colors.size());
  set_document_palette(std::move(exact->colors), tr("Extract palette"),
                       tr("Extracted %n color(s) from the image", nullptr, count));
}

void MainWindow::load_palette_from_file() {
  auto settings = app_settings();
  const auto last_dir = settings.value(QStringLiteral("palettes/lastDirectory")).toString();
  const auto path = QFileDialog::getOpenFileName(
      this, tr("Load Palette"), last_dir,
      tr("Palette Files (*.pal *.gpl *.hex *.act *.aco *.ase *.bmp);;All Files (*)"));
  if (path.isEmpty()) {
    return;
  }
  settings.setValue(QStringLiteral("palettes/lastDirectory"), QFileInfo(path).absolutePath());
  try {
    auto data = patchy::palette_io::read_palette_file(std::filesystem::path(path.toStdWString()));
    set_document_palette(std::move(data.colors), tr("Load palette"),
                         tr("Loaded palette %1").arg(QFileInfo(path).fileName()));
  } catch (const std::exception& error) {
    QMessageBox::warning(this, tr("Load Palette"),
                         tr("Could not load the palette file.\n%1").arg(QString::fromUtf8(error.what())));
  }
}

void MainWindow::save_palette_to_file() {
  const auto colors = displayed_palette_colors();
  if (colors.empty()) {
    return;
  }
  auto settings = app_settings();
  const auto last_dir = settings.value(QStringLiteral("palettes/lastDirectory")).toString();
  QString selected_filter;
  auto path = QFileDialog::getSaveFileName(
      this, tr("Save Palette"), last_dir,
      tr("GIMP Palette (*.gpl);;Hex Colors (*.hex);;JASC Palette (*.pal);;Adobe Color Table (*.act);;"
         "Adobe Color Swatches (*.aco);;PNG Swatch Strip (*.png)"),
      &selected_filter);
  if (path.isEmpty()) {
    return;
  }
  auto suffix = QFileInfo(path).suffix().toLower();
  if (suffix.isEmpty()) {
    // Derive the extension from the chosen filter, e.g. "... (*.gpl)".
    const auto star = selected_filter.indexOf(QStringLiteral("(*."));
    if (star >= 0) {
      suffix = selected_filter.mid(star + 3, selected_filter.indexOf(')') - star - 3).toLower();
      path += QLatin1Char('.') + suffix;
    }
  }
  settings.setValue(QStringLiteral("palettes/lastDirectory"), QFileInfo(path).absolutePath());
  try {
    if (suffix == QStringLiteral("png")) {
      constexpr int kCell = 16;
      QImage strip(static_cast<int>(colors.size()) * kCell, kCell, QImage::Format_RGB32);
      for (int index = 0; index < static_cast<int>(colors.size()); ++index) {
        const auto& color = colors[static_cast<std::size_t>(index)];
        for (int y = 0; y < kCell; ++y) {
          for (int x = 0; x < kCell; ++x) {
            strip.setPixel(index * kCell + x, y, qRgb(color.red, color.green, color.blue));
          }
        }
      }
      if (!strip.save(path)) {
        throw std::runtime_error("Could not write the PNG file");
      }
    } else {
      const auto format = patchy::palette_io::palette_format_for_extension(suffix.toStdString());
      if (!format.has_value()) {
        throw std::runtime_error("Unsupported palette file extension");
      }
      patchy::palette_io::write_palette_file(std::filesystem::path(path.toStdWString()), colors, *format,
                                             QFileInfo(path).completeBaseName().toStdString());
    }
    statusBar()->showMessage(tr("Saved palette %1").arg(QFileInfo(path).fileName()));
  } catch (const std::exception& error) {
    QMessageBox::warning(this, tr("Save Palette"),
                         tr("Could not save the palette file.\n%1").arg(QString::fromUtf8(error.what())));
  }
}

ImageSaveOptions MainWindow::image_save_defaults_for_document() {
  auto options = load_image_save_option_defaults();
  if (has_active_document() && document().palette_editing().has_value() &&
      !document().palette_editing()->palette.colors.empty()) {
    const auto count = document().palette_editing()->palette.colors.size();
    // The depth must match the indexed_palette mirror's source_bit_depth or the
    // BMP writer's imported-exact path will not engage.
    options.bmp_encoding = count <= 4    ? bmp::BmpEncoding::Indexed2
                           : count <= 16 ? bmp::BmpEncoding::Indexed4
                                         : bmp::BmpEncoding::Indexed8;
    options.bmp_palette_mode = bmp::BmpPaletteMode::Exact;
  }
  return options;
}

void MainWindow::persist_image_save_defaults(const ImageSaveOptions& options) {
  auto to_save = options;
  if (has_active_document() && document().palette_editing().has_value()) {
    // The indexed BMP choices were driven by this document's palette; keep the
    // user's own BMP defaults for everything else.
    const auto globals = load_image_save_option_defaults();
    to_save.bmp_encoding = globals.bmp_encoding;
    to_save.bmp_palette_mode = globals.bmp_palette_mode;
    to_save.bmp_palette_path = globals.bmp_palette_path;
  }
  save_image_save_option_defaults(to_save);
}

void MainWindow::convert_document_to_indexed() {
  if (!has_active_document()) {
    return;
  }
  const auto flattened = Compositor().flatten_rgb8(document());
  std::optional<Palette> current_palette;
  if (auto colors = displayed_palette_colors(); !colors.empty()) {
    current_palette = Palette{std::move(colors)};
  }
  const auto settings = request_palette_convert_settings(this, flattened, current_palette);
  if (!settings.has_value()) {
    refresh_palette_panel();  // restore the Mode menu check state after a cancel
    return;
  }

  push_undo_snapshot(tr("Convert to Indexed (Palette)"));
  auto& doc = document();
  DocumentPaletteEditing editing;
  editing.palette = settings->palette;
  editing.alpha_threshold = static_cast<std::uint8_t>(std::clamp(settings->alpha_threshold, 1, 255));
  editing.palette_revision = next_palette_revision();
  doc.palette_editing() = std::move(editing);
  patchy::sync_document_indexed_palette(doc);

  patchy::PaletteLut lut;
  lut.build(doc.palette_editing()->palette.colors);
  apply_palette_to_layers(doc.layers(), lut, settings->dither, doc.palette_editing()->alpha_threshold);
  palette_compliance_clean_ = true;

  if (canvas_ != nullptr) {
    canvas_->document_changed();
  }
  refresh_layer_list();
  refresh_palette_panel();
  refresh_document_info();
  statusBar()->showMessage(tr("Converted to indexed palette editing (%n color(s))", nullptr,
                              static_cast<int>(doc.palette_editing()->palette.colors.size())));
}

void MainWindow::convert_document_to_rgb() {
  if (!has_active_document() || !document().palette_editing().has_value()) {
    refresh_palette_panel();
    return;
  }
  push_undo_snapshot(tr("Convert to RGB Color"));
  // Pixels are untouched and the palette stays attached as document metadata
  // (indexed_palette), so Indexed -> RGB -> Indexed round-trips losslessly.
  patchy::sync_document_indexed_palette(document());
  document().palette_editing().reset();
  if (canvas_ != nullptr) {
    canvas_->document_changed();  // the display drops its palette quantization
  }
  refresh_palette_panel();
  refresh_document_info();
  statusBar()->showMessage(tr("Converted to RGB color; pixels are unchanged"));
}

void MainWindow::snap_layers_to_palette(bool active_layer_only) {
  if (!has_active_document() || canvas_ == nullptr) {
    return;
  }
  const auto& editing = document().palette_editing();
  if (!editing.has_value()) {
    return;
  }
  const auto* snap = canvas_->palette_snap_context();
  if (snap == nullptr || snap->lut == nullptr) {
    return;
  }
  push_undo_snapshot(active_layer_only ? tr("Snap layer to palette") : tr("Snap image to palette"));
  auto& doc = document();
  const auto threshold = editing->alpha_threshold;
  if (active_layer_only) {
    const auto active = doc.active_layer_id();
    auto* layer = active.has_value() ? doc.find_layer(*active) : nullptr;
    if (layer != nullptr && layer->kind() != LayerKind::Group && layer->kind() != LayerKind::Adjustment &&
        layer->pixels().width() > 0 && layer->pixels().format().channels >= 3) {
      (void)patchy::apply_palette_to_pixels(layer->pixels(), *snap->lut, patchy::PaletteDither::None, threshold);
    }
  } else {
    apply_palette_to_layers(doc.layers(), *snap->lut, patchy::PaletteDither::None, threshold);
  }
  canvas_->document_changed();
  refresh_layer_list();
  refresh_palette_panel();
  schedule_palette_compliance_check();
  statusBar()->showMessage(active_layer_only ? tr("Layer snapped to the palette")
                                             : tr("Image snapped to the palette"));
}

void MainWindow::maybe_offer_indexed_palette_adoption() {
  if (!has_active_document()) {
    return;
  }
  auto& doc = document();
  if (doc.palette_editing().has_value() || !doc.indexed_palette().has_value() ||
      doc.indexed_palette()->colors.empty()) {
    return;
  }
  auto settings = app_settings();
  const auto policy =
      settings.value(QStringLiteral("imports/adoptIndexedPalette"), QStringLiteral("ask")).toString();
  if (policy == QStringLiteral("never")) {
    return;
  }
  bool adopt = policy == QStringLiteral("always");
  if (!adopt) {
    QMessageBox box(this);
    box.setObjectName(QStringLiteral("adoptIndexedPaletteMessageBox"));
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Indexed Image"));
    box.setText(tr("This image uses a %n-color palette.", nullptr,
                   static_cast<int>(doc.indexed_palette()->colors.size())));
    box.setInformativeText(tr("Keep editing with the palette? Painting will snap to its colors; you can switch "
                              "back any time with Image > Mode > RGB Color."));
    auto* keep_button = box.addButton(tr("Use Palette"), QMessageBox::AcceptRole);
    box.addButton(tr("Edit as RGB"), QMessageBox::RejectRole);
    auto* remember = new QCheckBox(tr("Do this for every indexed image"), &box);
    box.setCheckBox(remember);
    exec_dialog(box);
    adopt = box.clickedButton() == keep_button;
    if (remember->isChecked()) {
      settings.setValue(QStringLiteral("imports/adoptIndexedPalette"),
                        adopt ? QStringLiteral("always") : QStringLiteral("never"));
    }
    if (!adopt) {
      return;
    }
  }

  DocumentPaletteEditing editing;
  editing.palette.colors = doc.indexed_palette()->colors;
  editing.palette_revision = next_palette_revision();
  doc.palette_editing() = std::move(editing);
  patchy::sync_document_indexed_palette(doc);
  palette_compliance_clean_ = true;
  if (canvas_ != nullptr) {
    canvas_->document_changed();  // the display picks up palette quantization
  }
  refresh_palette_panel();
  schedule_palette_compliance_check();
  statusBar()->showMessage(tr("Editing with the image's palette"));
}

void MainWindow::refresh_palette_panel() {
  if (palette_panel_ == nullptr) {
    return;
  }
  const auto colors = displayed_palette_colors();
  const auto mode_active = has_active_document() && document().palette_editing().has_value();
  palette_panel_->set_palette(colors, mode_active);
  std::optional<RgbColor> highlight;
  if (mode_active && canvas_ != nullptr) {
    highlight = rgb_from_qcolor(canvas_->primary_color());
  }
  palette_panel_->set_highlight_color(highlight);
  if (image_mode_rgb_action_ != nullptr && image_mode_indexed_action_ != nullptr) {
    const QSignalBlocker rgb_blocker(image_mode_rgb_action_);
    const QSignalBlocker indexed_blocker(image_mode_indexed_action_);
    image_mode_rgb_action_->setChecked(!mode_active);
    image_mode_indexed_action_->setChecked(mode_active);
  }
  if (snap_image_to_palette_action_ != nullptr && snap_layer_to_palette_action_ != nullptr) {
    snap_image_to_palette_action_->setEnabled(mode_active);
    snap_layer_to_palette_action_->setEnabled(mode_active);
  }
  refresh_palette_mode_chip();
}

void MainWindow::refresh_palette_mode_chip() {
  if (palette_mode_chip_ == nullptr) {
    return;
  }
  const auto mode_active = has_active_document() && document().palette_editing().has_value();
  if (!mode_active) {
    palette_mode_chip_->hide();
    return;
  }
  const auto count = static_cast<int>(document().palette_editing()->palette.colors.size());
  auto text = tr("Palette: %n color(s)", nullptr, count);
  if (!palette_compliance_clean_) {
    text += tr(" (off-palette)");
  }
  palette_mode_chip_->setText(text);
  palette_mode_chip_->setToolTip(
      palette_compliance_clean_
          ? tr("Painting is constrained to the document palette. Click to show the Palette panel.")
          : tr("Some layers contain colors outside the palette (filters, layer styles, or text can cause this). "
               "Use Image > Snap Image to Palette to fix them. Click to show the Palette panel."));
  palette_mode_chip_->show();
}

void MainWindow::schedule_palette_compliance_check() {
  if (palette_compliance_timer_ == nullptr) {
    return;
  }
  if (!has_active_document() || !document().palette_editing().has_value()) {
    palette_compliance_timer_->stop();
    return;
  }
  palette_compliance_timer_->start();
}

void MainWindow::run_palette_compliance_check() {
  if (!has_active_document() || canvas_ == nullptr || !document().palette_editing().has_value()) {
    return;
  }
  // The scan is advisory: documents past ~4 Mpx skip it (no warning shown) so the
  // debounce timer never causes a visible hitch on huge canvases.
  const auto pixel_count = static_cast<std::int64_t>(document().width()) * document().height();
  bool clean = true;
  if (pixel_count <= 4'000'000) {
    const auto* snap = canvas_->palette_snap_context();
    if (snap != nullptr && snap->lut != nullptr) {
      clean = layers_are_palette_clean(document().layers(), *snap->lut);
    }
  }
  if (clean != palette_compliance_clean_) {
    palette_compliance_clean_ = clean;
    refresh_palette_mode_chip();
  }
}


}  // namespace patchy::ui
