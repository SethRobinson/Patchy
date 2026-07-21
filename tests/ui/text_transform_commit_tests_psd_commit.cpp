#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "ui/smart_object_render.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_layer_effects.hpp"
#include "core/style_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/color_panel.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/filter_look_library.hpp"
#include "ui/font_picker.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/selection_outline.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/zoom_status_bar.hpp"
#include "filters/builtin_filters.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "render/compositor.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDataStream>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QInputDialog>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QLayout>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocale>
#include <QSizeGrip>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QThread>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointingDevice>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_psd_centered_point_text_keeps_center_on_commit() {
  // Regression (reported repro): tlm-main-mockup.psd has one centered point-text layer holding the
  // five menu lines ("Continue Career" ... "Quit") in a font that is not installed.  Editing it
  // (accepting the substitution warning) and applying the edit rendered all five lines flush left
  // and shifted the block, even though the toolbar still showed Center; re-entering the edit then
  // showed doubled/overlapping text because the editor, caret layout, and committed raster each
  // laid the text out differently.
  const auto path = patchy::test::local_psd_fixture_path("tlm-main-mockup.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);

  patchy::Rect text_bounds{};
  patchy::LayerId menu_id = 0;
  bool found = false;
  std::function<void(const std::vector<patchy::Layer>&)> find_menu =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (!found) {
            if (const auto it = layer.metadata().find(patchy::kLayerMetadataText);
                it != layer.metadata().end() && it->second.find("Continue Career") != std::string::npos) {
              text_bounds = layer.bounds();
              menu_id = layer.id();
              found = true;
            }
          }
          find_menu(layer.children());
        }
      };
  find_menu(document.layers());
  if (!found) {
    return;  // fixture layout changed; nothing to assert against
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("TLM Centered Menu"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* menu_before = live_document.find_layer(menu_id);
  CHECK(menu_before != nullptr);
  const auto original_visible =
      alpha_pixel_bounds_in_rows(menu_before->pixels(), 0, menu_before->pixels().height());
  CHECK(original_visible.has_value());
  if (!original_visible.has_value()) {
    return;
  }
  const auto original_center_x =
      menu_before->bounds().x + original_visible->left() + original_visible->width() / 2.0;
  const auto original_bands = alpha_row_bands(menu_before->pixels());
  CHECK(original_bands.size() == 5);
  // Deep-copy the imported pixels: an Escape session below must leave them byte-identical.
  const std::vector<std::uint8_t> original_bytes(menu_before->pixels().data().begin(),
                                                 menu_before->pixels().data().end());

  // Session 1: enter the edit (accepting the substitution warning) and immediately Escape.
  // Entering must swap the display to the live substituted-font render right away -- before any
  // keystroke -- and Escape must restore Photoshop's original pixels untouched.
  live_document.set_active_layer(menu_id);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint click_doc(text_bounds.x + text_bounds.width / 2, text_bounds.y + 12);
  const auto hit_point = canvas->widget_position_for_document_point(click_doc);
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(250);

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  auto* entry_preview = preview_layer_for_editor(live_document, *editor);
  CHECK(entry_preview != nullptr);
  if (entry_preview != nullptr) {
    const auto entry_visible =
        alpha_pixel_bounds_in_rows(entry_preview->pixels(), 0, entry_preview->pixels().height());
    CHECK(entry_visible.has_value());
    if (entry_visible.has_value()) {
      const auto entry_center_x =
          entry_preview->bounds().x + entry_visible->left() + entry_visible->width() / 2.0;
      CHECK(std::abs(entry_center_x - original_center_x) <= 6.0);
    }
  }
  {
    auto* menu_during = live_document.find_layer(menu_id);
    CHECK(menu_during != nullptr);
    CHECK(menu_during == nullptr || !menu_during->visible());
  }
  save_widget_artifact("ui_tlm_centered_menu_entry_live", *canvas);
  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  process_events_for(100);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  {
    auto* menu_restored = live_document.find_layer(menu_id);
    CHECK(menu_restored != nullptr);
    if (menu_restored == nullptr) {
      return;
    }
    CHECK(menu_restored->visible());
    const std::vector<std::uint8_t> restored_bytes(menu_restored->pixels().data().begin(),
                                                   menu_restored->pixels().data().end());
    CHECK(restored_bytes == original_bytes);
  }

  // Session 2: apply without changing anything (switching tools applies).  The live substituted
  // render the session was showing is kept -- the layer becomes a Patchy raster in place instead
  // of snapping back to Photoshop's pixels.
  live_document.set_active_layer(menu_id);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(250);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(100);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  {
    auto* menu_applied = live_document.find_layer(menu_id);
    CHECK(menu_applied != nullptr);
    if (menu_applied == nullptr) {
      return;
    }
    CHECK(menu_applied->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
    const auto applied_visible =
        alpha_pixel_bounds_in_rows(menu_applied->pixels(), 0, menu_applied->pixels().height());
    CHECK(applied_visible.has_value());
    if (applied_visible.has_value()) {
      const auto applied_center_x =
          menu_applied->bounds().x + applied_visible->left() + applied_visible->width() / 2.0;
      CHECK(std::abs(applied_center_x - original_center_x) <= 6.0);
    }
  }
  save_widget_artifact("ui_tlm_centered_menu_unchanged_apply", *canvas);

  // Session 3: a real edit that gets committed.
  live_document.set_active_layer(menu_id);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(200);

  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->toPlainText().contains(QStringLiteral("Continue Career")));

  // A real edit hands the session to the live baked preview (the same rasterizer the commit
  // uses); the editor widget's own glyph painting would lay the centered lines out differently.
  auto cursor = editor->textCursor();
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  cursor.insertText(QStringLiteral("!"));
  QApplication::processEvents();
  process_events_for(300);
  CHECK(editor->property("patchy.previewPaintsText").toBool());
  auto* preview = preview_layer_for_editor(live_document, *editor);
  CHECK(preview != nullptr);
  save_widget_artifact("ui_tlm_centered_menu_editing", *canvas);
  if (preview != nullptr) {
    const auto preview_visible = alpha_pixel_bounds_in_rows(preview->pixels(), 0, preview->pixels().height());
    CHECK(preview_visible.has_value());
    if (preview_visible.has_value()) {
      const auto preview_center_x =
          preview->bounds().x + preview_visible->left() + preview_visible->width() / 2.0;
      CHECK(std::abs(preview_center_x - original_center_x) <= 6.0);
    }
  }

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(100);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  save_widget_artifact("ui_tlm_centered_menu_committed", *canvas);

  auto* menu_after = live_document.find_layer(menu_id);
  CHECK(menu_after != nullptr);
  if (menu_after == nullptr) {
    return;
  }
  CHECK(menu_after->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  CHECK(QString::fromStdString(menu_after->metadata().at(patchy::kLayerMetadataTextParagraphRuns))
            .contains(QStringLiteral("center")));

  const auto bands = alpha_row_bands(menu_after->pixels());
  CHECK(bands.size() == 5);
  if (bands.size() == 5) {
    std::vector<QRect> extents;
    for (const auto& band : bands) {
      const auto extent = alpha_pixel_bounds_in_rows(menu_after->pixels(), band.top, band.bottom);
      CHECK(extent.has_value());
      if (!extent.has_value()) {
        return;
      }
      extents.push_back(*extent);
    }
    // Every line centers on the same axis; the regression rendered them all flush left, which put
    // the short lines' midpoints far left of the long lines'.
    const auto reference_center = extents.front().left() + extents.front().width() / 2.0;
    for (const auto& extent : extents) {
      CHECK(std::abs(extent.left() + extent.width() / 2.0 - reference_center) <= 5.0);
    }
    // Sanity: the lines genuinely differ in width ("Quit" vs "Continue Career!"), so the centering
    // assertion above is meaningful.
    CHECK(extents.back().width() < extents.front().width() - 60);
  }
  const auto committed_visible =
      alpha_pixel_bounds_in_rows(menu_after->pixels(), 0, menu_after->pixels().height());
  CHECK(committed_visible.has_value());
  if (!committed_visible.has_value()) {
    return;
  }
  const auto committed_center_x =
      menu_after->bounds().x + committed_visible->left() + committed_visible->width() / 2.0;
  // The block stays centered where Photoshop drew it (the justification point is the anchor).
  CHECK(std::abs(committed_center_x - original_center_x) <= 6.0);

  // Re-entering the edit must land the live preview on the committed glyphs -- no shift, no
  // doubled text -- and a further text change must keep the block pinned to the same center even
  // though the PSD source metadata was cleared by the first commit.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto reedit_point = canvas->widget_position_for_document_point(
      QPoint(menu_after->bounds().x + menu_after->bounds().width / 2, menu_after->bounds().y + 12));
  send_mouse(*canvas, QEvent::MouseButtonPress, reedit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, reedit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(300);
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  auto* reedit_preview = preview_layer_for_editor(live_document, *editor);
  CHECK(reedit_preview != nullptr);
  if (reedit_preview != nullptr) {
    const auto reedit_visible =
        alpha_pixel_bounds_in_rows(reedit_preview->pixels(), 0, reedit_preview->pixels().height());
    CHECK(reedit_visible.has_value());
    if (reedit_visible.has_value()) {
      const auto reedit_center_x =
          reedit_preview->bounds().x + reedit_visible->left() + reedit_visible->width() / 2.0;
      CHECK(std::abs(reedit_center_x - committed_center_x) <= 4.0);
    }
  }
  save_widget_artifact("ui_tlm_centered_menu_reedit", *canvas);

  auto reedit_cursor = editor->textCursor();
  reedit_cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(reedit_cursor);
  reedit_cursor.insertText(QStringLiteral("?"));
  QApplication::processEvents();
  process_events_for(300);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(100);

  auto* menu_final = live_document.find_layer(menu_id);
  CHECK(menu_final != nullptr);
  if (menu_final == nullptr) {
    return;
  }
  const auto final_visible = alpha_pixel_bounds_in_rows(menu_final->pixels(), 0, menu_final->pixels().height());
  CHECK(final_visible.has_value());
  if (!final_visible.has_value()) {
    return;
  }
  const auto final_center_x = menu_final->bounds().x + final_visible->left() + final_visible->width() / 2.0;
  CHECK(std::abs(final_center_x - committed_center_x) <= 5.0);
  save_widget_artifact("ui_tlm_centered_menu_recommitted", *canvas);
}

// Fraction of inked pixels whose alpha sits in the anti-aliasing midrange. A crisp render keeps
// solid stroke cores (low fraction); a base-size raster resampled up through a ~4x transform
// turns nearly every pixel into a soft ramp (the blurry-conversion bug).
double mid_alpha_fraction(const patchy::PixelBuffer& pixels) {
  const auto channels = pixels.format().channels;
  if (pixels.empty() || (channels != 1U && channels < 4U)) {
    return 0.0;
  }
  const auto alpha_channel = channels == 1U ? 0U : 3U;
  const auto bytes = pixels.data();
  const auto stride = pixels.stride_bytes();
  std::size_t inked = 0;
  std::size_t mid = 0;
  for (int y = 0; y < pixels.height(); ++y) {
    const auto row_offset = static_cast<std::size_t>(y) * stride;
    for (int x = 0; x < pixels.width(); ++x) {
      const auto offset = row_offset + static_cast<std::size_t>(x) * channels + alpha_channel;
      if (offset >= bytes.size()) {
        continue;
      }
      const auto alpha = bytes[offset];
      if (alpha > 16U) {
        ++inked;
        if (alpha >= 40U && alpha <= 215U) {
          ++mid;
        }
      }
    }
  }
  return inked > 0 ? static_cast<double>(mid) / static_cast<double>(inked) : 0.0;
}

// Shared harness for the Photoshop text-model tests: open a PSD, find the text layer whose
// content contains `needle`, capture the ink row bands of Photoshop's own raster (document
// space), run `commit_cycles` unchanged edit -> apply cycles (the "convert to Patchy text"
// flow, which re-renders with Patchy's engine), and capture the re-rendered bands the same way.
struct PhotoshopTextCommitProbe {
  std::vector<AlphaRowBand> original_bands;
  std::vector<AlphaRowBand> committed_bands;
  std::vector<std::vector<AlphaRowBand>> cycle_bands;  // bands after each commit cycle
  patchy::Rect original_ink;
  patchy::Rect committed_ink;
  double committed_mid_alpha_fraction{0.0};
  int committed_box_width_metadata{0};
};

std::optional<PhotoshopTextCommitProbe> run_photoshop_text_commit_probe(const std::filesystem::path& path,
                                                                        const char* needle,
                                                                        double zoom,
                                                                        const char* artifact_name,
                                                                        int commit_cycles = 1) {
  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::LayerId layer_id = 0;
  bool found = false;
  std::function<void(const std::vector<patchy::Layer>&)> find_text_layer =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (!found) {
            if (const auto it = layer.metadata().find(patchy::kLayerMetadataText);
                it != layer.metadata().end() && it->second.find(needle) != std::string::npos) {
              layer_id = layer.id();
              found = true;
            }
          }
          find_text_layer(layer.children());
        }
      };
  find_text_layer(document.layers());
  CHECK(found);
  if (!found) {
    return std::nullopt;
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Photoshop Text Model"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(zoom);
  QApplication::processEvents();

  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* original = live_document.find_layer(layer_id);
  CHECK(original != nullptr);
  if (original == nullptr) {
    return std::nullopt;
  }
  PhotoshopTextCommitProbe probe;
  probe.original_bands = alpha_row_bands(original->pixels());
  for (auto& band : probe.original_bands) {
    band.top += original->bounds().y;
    band.bottom += original->bounds().y;
  }
  const auto original_visible = alpha_pixel_bounds_in_rows(original->pixels(), 0, original->pixels().height());
  CHECK(original_visible.has_value());
  if (!original_visible.has_value()) {
    return std::nullopt;
  }
  probe.original_ink = patchy::Rect{original->bounds().x + original_visible->left(),
                                    original->bounds().y + original_visible->top(),
                                    original_visible->width(), original_visible->height()};

  for (int cycle = 0; cycle < commit_cycles; ++cycle) {
    auto* live_layer = live_document.find_layer(layer_id);
    CHECK(live_layer != nullptr);
    if (live_layer == nullptr) {
      return std::nullopt;
    }
    const auto bounds_now = live_layer->bounds();
    live_document.set_active_layer(layer_id);
    require_action_by_text(window, QStringLiteral("Type"))->trigger();
    const QPoint click_doc(bounds_now.x + bounds_now.width / 2, bounds_now.y + 12);
    const auto hit_point = canvas->widget_position_for_document_point(click_doc);
    accept_missing_psd_text_font_warning_if_present();
    send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    process_events_for(250);
    auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) {
      return std::nullopt;
    }
    // The canvas activates the TOPMOST text layer under the click (Photoshop-style), which may
    // not be the probed layer when text layers overlap -- keep the probes on unoccluded layers.
    CHECK(editor->property("patchy.editingLayerId").toULongLong() == static_cast<qulonglong>(layer_id));
    // Applying the unchanged session commits Patchy's own render of the layer (point text shows
    // the live layout from entry; see commit_text_editor).
    require_action_by_text(window, QStringLiteral("Move"))->trigger();
    QApplication::processEvents();
    process_events_for(150);
    CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
    if (auto* cycled = live_document.find_layer(layer_id); cycled != nullptr) {
      auto bands = alpha_row_bands(cycled->pixels());
      for (auto& band : bands) {
        band.top += cycled->bounds().y;
        band.bottom += cycled->bounds().y;
      }
      probe.cycle_bands.push_back(std::move(bands));
    }
  }

  auto* committed = live_document.find_layer(layer_id);
  CHECK(committed != nullptr);
  if (committed == nullptr) {
    return std::nullopt;
  }
  CHECK(committed->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  probe.committed_bands = alpha_row_bands(committed->pixels());
  for (auto& band : probe.committed_bands) {
    band.top += committed->bounds().y;
    band.bottom += committed->bounds().y;
  }
  const auto committed_visible =
      alpha_pixel_bounds_in_rows(committed->pixels(), 0, committed->pixels().height());
  CHECK(committed_visible.has_value());
  if (!committed_visible.has_value()) {
    return std::nullopt;
  }
  probe.committed_ink = patchy::Rect{committed->bounds().x + committed_visible->left(),
                                     committed->bounds().y + committed_visible->top(),
                                     committed_visible->width(), committed_visible->height()};
  probe.committed_mid_alpha_fraction = mid_alpha_fraction(committed->pixels());
  if (const auto width_value = committed->metadata().find(patchy::kLayerMetadataTextBoxWidth);
      width_value != committed->metadata().end()) {
    probe.committed_box_width_metadata = std::atoi(width_value->second.c_str());
  }
  save_widget_artifact(artifact_name, *canvas);
  return probe;
}

void ui_psd_text_fixed_leading_commit_matches_photoshop_row_bands() {
  // photoshop-text-point-fixed-leading.psd: PS 2026, point text "HHHH\rHHHH\rHHHH", Arial 24pt,
  // fixed leading 40, anchor baseline at y=60. Photoshop renders H-bands ending on the baselines
  // 60/100/140 (the baseline advance IS the leading). Patchy ignored leading entirely (Qt natural
  // spacing ~28px), so the committed block collapsed; with the Photoshop layout model the
  // re-rendered bands must land on Photoshop's, row for row.
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-text-point-fixed-leading.psd");
  const auto probe = run_photoshop_text_commit_probe(path, "HHHH", 1.0, "ui_psd_text_fixed_leading_commit");
  if (!probe.has_value()) {
    return;
  }
  CHECK(probe->original_bands.size() == 3);
  CHECK(probe->committed_bands.size() == 3);
  if (probe->original_bands.size() != 3 || probe->committed_bands.size() != 3) {
    return;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(std::abs(probe->committed_bands[i].bottom - probe->original_bands[i].bottom) <= 2);
    CHECK(std::abs(probe->committed_bands[i].top - probe->original_bands[i].top) <= 2);
  }
  // Baseline advances = the fixed leading (40), not Qt's natural ~29.
  CHECK(std::abs((probe->committed_bands[1].bottom - probe->committed_bands[0].bottom) - 40) <= 1);
  CHECK(std::abs((probe->committed_bands[2].bottom - probe->committed_bands[1].bottom) - 40) <= 1);
}

void ui_psd_text_auto_leading_commit_matches_photoshop_row_bands() {
  // photoshop-text-point-auto-leading.psd: same text but auto leading (1.2 x 24 = 28.8,
  // sub-pixel exact in Photoshop -- baselines 60 / 88.8 / 117.6).
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-text-point-auto-leading.psd");
  const auto probe = run_photoshop_text_commit_probe(path, "HHHH", 1.0, "ui_psd_text_auto_leading_commit");
  if (!probe.has_value()) {
    return;
  }
  CHECK(probe->original_bands.size() == 3);
  CHECK(probe->committed_bands.size() == 3);
  if (probe->original_bands.size() != 3 || probe->committed_bands.size() != 3) {
    return;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(std::abs(probe->committed_bands[i].bottom - probe->original_bands[i].bottom) <= 2);
  }
}

void ui_psd_text_transformed_commit_keeps_photoshop_leading() {
  // photoshop-text-point-transformed.psd: 24pt auto-leading text free-transformed to 200% x 150%
  // (TySh transform xx=2, yy=1.5; engine values unchanged). Effective em 36px, baseline advance
  // 43.2px. The old average-of-axes scale (1.75) distorted both; the fold must use the vertical
  // scale for sizes/leading and keep the horizontal stretch in the residual matrix.
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-text-point-transformed.psd");
  const auto probe = run_photoshop_text_commit_probe(path, "HHHH", 1.0, "ui_psd_text_transformed_commit");
  if (!probe.has_value()) {
    return;
  }
  CHECK(probe->original_bands.size() == 3);
  CHECK(probe->committed_bands.size() == 3);
  if (probe->original_bands.size() != 3 || probe->committed_bands.size() != 3) {
    return;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(std::abs(probe->committed_bands[i].bottom - probe->original_bands[i].bottom) <= 2);
    // Band height tracks the scaled em (caps ~26px tall), not the raw 24pt (~17px).
    CHECK(std::abs((probe->committed_bands[i].bottom - probe->committed_bands[i].top) -
                   (probe->original_bands[i].bottom - probe->original_bands[i].top)) <= 2);
  }
  // The 200% horizontal stretch survives (ink width ~132px, raw would be ~66).
  CHECK(std::abs(probe->committed_ink.width - probe->original_ink.width) <= 5);
}

void ui_restaurant_menu_dishes_commit_matches_photoshop_bands_if_available() {
  // The reported repro: the CMYK restaurant menu's 'Dishes' point-text layer (alternating
  // default-size names / 7.24564 descriptions, fixed leadings 19.43333/11.1, TySh scale
  // ~4.48). Converting it rendered every line at the same too-small size with collapsed
  // spacing. The fonts (Campanile) are not installed, so glyph shapes substitute -- but the
  // baseline structure must match Photoshop's raster: same line count, same baseline
  // positions within a few pixels, and the alternating 87/50px advance pattern.
  const auto path = patchy::test::local_psd_fixture_path("restaurant-menu-inside.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  // The description face (Candara-BoldItalic) is a stock Windows font: registering it keeps the
  // description lines' descender ink (italic f, the slashes) comparable against Photoshop's
  // raster. The name face (Campanile) stays unavailable -- name rows compare on baselines.
  // TWO commit cycles: the re-edit path runs without the PSD source metadata (cleared by the
  // first commit) and must neither blur (crisp render, not a base-size resample) nor blow the
  // stored geometry up (the mixed-units box width bug mapped the document width through the
  // transform a second time).
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::Candara);
  const auto probe =
      run_photoshop_text_commit_probe(path, "Braised Leeks", 0.25, "ui_restaurant_menu_dishes_commit", 3);
  if (!probe.has_value()) {
    return;
  }
  std::printf("  dishes bands (orig -> committed):\n");
  for (std::size_t i = 0; i < std::max(probe->original_bands.size(), probe->committed_bands.size()); ++i) {
    const auto orig = i < probe->original_bands.size()
                          ? QStringLiteral("[%1,%2)").arg(probe->original_bands[i].top).arg(probe->original_bands[i].bottom)
                          : QStringLiteral("-");
    const auto committed = i < probe->committed_bands.size()
                               ? QStringLiteral("[%1,%2)").arg(probe->committed_bands[i].top).arg(probe->committed_bands[i].bottom)
                               : QStringLiteral("-");
    std::printf("    %2zu: %s -> %s\n", i, orig.toUtf8().constData(), committed.toUtf8().constData());
  }
  CHECK(probe->original_bands.size() == 12);
  CHECK(probe->committed_bands.size() == probe->original_bands.size());
  if (probe->committed_bands.size() != probe->original_bands.size()) {
    return;
  }
  for (std::size_t i = 0; i < probe->original_bands.size(); ++i) {
    // Band bottoms ride the baselines (leading model); substitute-font descenders and the
    // one-time anchor settle between the conversion (PSD-geometry-aligned placement) and
    // re-edits (pure transform placement) move them a few pixels at most.
    CHECK(std::abs(probe->committed_bands[i].bottom - probe->original_bands[i].bottom) <= 10);
    CHECK(std::abs(probe->committed_bands[i].top - probe->original_bands[i].top) <= 12);
  }
  // Total block height within 2% of Photoshop's 785px (the collapsed-spacing bug halved it).
  const auto original_height = probe->original_bands.back().bottom - probe->original_bands.front().top;
  const auto committed_height = probe->committed_bands.back().bottom - probe->committed_bands.front().top;
  CHECK(std::abs(committed_height - original_height) <= original_height / 50);
  // Repeated edit/apply cycles must not walk or degrade the layer: after the first re-edit's
  // one-time anchor settle, every further cycle reproduces identical band geometry.
  CHECK(probe->cycle_bands.size() >= 3);
  if (probe->cycle_bands.size() >= 3) {
    const auto& second = probe->cycle_bands[probe->cycle_bands.size() - 2];
    const auto& third = probe->cycle_bands.back();
    CHECK(second.size() == third.size());
    if (second.size() == third.size()) {
      for (std::size_t i = 0; i < second.size(); ++i) {
        CHECK(std::abs(third[i].top - second[i].top) <= 1);
        CHECK(std::abs(third[i].bottom - second[i].bottom) <= 1);
      }
    }
  }
  // Crisp through-transform render even with substituted fonts: the resample fallback (glyphs
  // rendered at engine size and scaled up ~4.5x) leaves almost no solid stroke cores.
  std::printf("  dishes committed mid-alpha fraction %.3f, stored box width %d\n",
              probe->committed_mid_alpha_fraction, probe->committed_box_width_metadata);
  CHECK(probe->committed_mid_alpha_fraction <= 0.65);
  // The stored box width lives in the runs' text-local space (ideal width ~140), not document
  // pixels (~630): the mixed-units value made the next session's edit rect several times wider
  // than the text.
  CHECK(probe->committed_box_width_metadata > 0);
  CHECK(probe->committed_box_width_metadata < 300);
}

void ui_psd_text_box_and_tracking_rasterize_match_photoshop() {
  // Rasterize (the metadata renderer, no editor session) against two more COM-authored probes:
  // photoshop-text-box-auto-leading.psd pins the box-text first baseline (box top + OS/2
  // sTypoAscender x size -- Qt's ascent() is usWinAscent and sits the line ~4px lower), and
  // photoshop-text-tracking.psd pins tracking (1/1000 em per glyph gap; the tracked line is
  // ~43px wider than the plain one at 24pt tracking 200).
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  {
    auto document = patchy::psd::DocumentIo::read_file(
        patchy::test::committed_psd_fixture_path("photoshop-text-box-auto-leading.psd"));
    patchy::LayerId text_layer_id = 0;
    std::vector<AlphaRowBand> original_bands;
    for (const auto& layer : document.layers()) {
      if (patchy::layer_is_text(layer)) {
        text_layer_id = layer.id();
        original_bands = alpha_row_bands(layer.pixels());
        for (auto& band : original_bands) {
          band.top += layer.bounds().y;
          band.bottom += layer.bounds().y;
        }
      }
    }
    CHECK(original_bands.size() == 2);

    patchy::ui::MainWindow window;
    show_window(window);
    window.add_document_session(std::move(document), QStringLiteral("Box First Baseline"));
    auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
    live_document.set_active_layer(text_layer_id);
    auto* rasterize = window.findChild<QAction*>(QStringLiteral("layerRasterizeAction"));
    CHECK(rasterize != nullptr);
    if (rasterize == nullptr) {
      return;
    }
    rasterize->trigger();
    QApplication::processEvents();
    auto* rasterized = live_document.find_layer(text_layer_id);
    CHECK(rasterized != nullptr);
    if (rasterized == nullptr) {
      return;
    }
    auto committed_bands = alpha_row_bands(rasterized->pixels());
    for (auto& band : committed_bands) {
      band.top += rasterized->bounds().y;
      band.bottom += rasterized->bounds().y;
    }
    CHECK(committed_bands.size() == original_bands.size());
    if (committed_bands.size() == original_bands.size()) {
      for (std::size_t i = 0; i < original_bands.size(); ++i) {
        CHECK(std::abs(committed_bands[i].bottom - original_bands[i].bottom) <= 3);
        CHECK(std::abs(committed_bands[i].top - original_bands[i].top) <= 3);
      }
    }
  }
  {
    auto document = patchy::psd::DocumentIo::read_file(
        patchy::test::committed_psd_fixture_path("photoshop-text-tracking.psd"));
    struct TrackedLayer {
      patchy::LayerId id{0};
      int original_width{0};
    };
    std::vector<TrackedLayer> tracked;
    for (const auto& layer : document.layers()) {
      if (patchy::layer_is_text(layer)) {
        const auto ink = alpha_pixel_bounds_in_rows(layer.pixels(), 0, layer.pixels().height());
        CHECK(ink.has_value());
        if (ink.has_value()) {
          tracked.push_back(TrackedLayer{layer.id(), ink->width()});
        }
      }
    }
    CHECK(tracked.size() == 2);
    if (tracked.size() != 2) {
      return;
    }
    // The fixture's layers: plain (ink ~170px) and tracking 200 (~213px).
    const auto widest = std::max(tracked[0].original_width, tracked[1].original_width);
    const auto narrowest = std::min(tracked[0].original_width, tracked[1].original_width);
    CHECK(widest - narrowest >= 35);

    patchy::ui::MainWindow window;
    show_window(window);
    window.add_document_session(std::move(document), QStringLiteral("Tracking"));
    auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
    auto* rasterize = window.findChild<QAction*>(QStringLiteral("layerRasterizeAction"));
    CHECK(rasterize != nullptr);
    if (rasterize == nullptr) {
      return;
    }
    for (const auto& entry : tracked) {
      live_document.set_active_layer(entry.id);
      rasterize->trigger();
      QApplication::processEvents();
      auto* rasterized = live_document.find_layer(entry.id);
      CHECK(rasterized != nullptr);
      if (rasterized == nullptr) {
        continue;
      }
      const auto ink = alpha_pixel_bounds_in_rows(rasterized->pixels(), 0, rasterized->pixels().height());
      CHECK(ink.has_value());
      if (ink.has_value()) {
        CHECK(std::abs(ink->width() - entry.original_width) <= 4);
      }
    }
  }
}

void ui_psd_text_hv_scale_rasterize_matches_photoshop() {
  // photoshop-text-hv-scale.psd: PS 2026, point text "HHHH\rHHHH", Arial 24pt with the
  // character panel's Horizontal Scale 80% / Vertical Scale 150%. COM-calibrated rules:
  // glyph height x V (caps ~26px), glyph width x H (ink ~53px wide), auto leading stays
  // 1.2 x FontSize = 28.8 (unscaled by V). Ignoring these rendered the SNES box template's
  // 90%-width text ~11% too wide (stretched down its rotated axis).
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-text-hv-scale.psd"));
  patchy::LayerId text_layer_id = 0;
  std::vector<AlphaRowBand> original_bands;
  int original_ink_width = 0;
  for (const auto& layer : document.layers()) {
    if (patchy::layer_is_text(layer)) {
      text_layer_id = layer.id();
      original_bands = alpha_row_bands(layer.pixels());
      for (auto& band : original_bands) {
        band.top += layer.bounds().y;
        band.bottom += layer.bounds().y;
      }
      if (const auto ink = alpha_pixel_bounds_in_rows(layer.pixels(), 0, layer.pixels().height());
          ink.has_value()) {
        original_ink_width = ink->width();
      }
    }
  }
  CHECK(original_bands.size() == 2);
  CHECK(original_ink_width > 0);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("HV Scale"));
  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  live_document.set_active_layer(text_layer_id);
  auto* rasterize = window.findChild<QAction*>(QStringLiteral("layerRasterizeAction"));
  CHECK(rasterize != nullptr);
  if (rasterize == nullptr) {
    return;
  }
  rasterize->trigger();
  QApplication::processEvents();
  auto* rasterized = live_document.find_layer(text_layer_id);
  CHECK(rasterized != nullptr);
  if (rasterized == nullptr) {
    return;
  }
  auto committed_bands = alpha_row_bands(rasterized->pixels());
  for (auto& band : committed_bands) {
    band.top += rasterized->bounds().y;
    band.bottom += rasterized->bounds().y;
  }
  CHECK(committed_bands.size() == original_bands.size());
  if (committed_bands.size() == original_bands.size()) {
    for (std::size_t i = 0; i < original_bands.size(); ++i) {
      // Band heights carry the 150% vertical glyph scale; bottoms ride the unscaled leading.
      CHECK(std::abs(committed_bands[i].bottom - original_bands[i].bottom) <= 2);
      CHECK(std::abs((committed_bands[i].bottom - committed_bands[i].top) -
                     (original_bands[i].bottom - original_bands[i].top)) <= 2);
    }
  }
  const auto committed_ink =
      alpha_pixel_bounds_in_rows(rasterized->pixels(), 0, rasterized->pixels().height());
  CHECK(committed_ink.has_value());
  if (committed_ink.has_value()) {
    // The 80% horizontal scale: without it the lines render ~25% too wide.
    CHECK(std::abs(committed_ink->width() - original_ink_width) <= 3);
  }
}

void ui_snes_box_rotated_hscale_commit_matches_if_available() {
  // The SNES box template's German blurb: point text with Horizontal Scale 90%, fixed leading,
  // rotated 90 degrees (transform is a pure rotation x 1.284). Ignoring the 90% width made the
  // re-render ~11% longer along its rotated (screen-vertical) axis. Arial Black is a stock
  // face here, so the converted ink box must land on Photoshop's within a few pixels on both
  // axes -- and the crisp path must hold through the rotation.
  const auto path = patchy::test::local_psd_fixture_path("snes-box-a3.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::ArialBlack);
  const auto probe =
      run_photoshop_text_commit_probe(path, "Mit deutschen", 0.25, "ui_snes_box_rotated_commit");
  if (!probe.has_value()) {
    return;
  }
  std::printf("  snes blurb ink: orig %dx%d at (%d,%d) -> committed %dx%d at (%d,%d), mid-alpha %.3f\n",
              probe->original_ink.width, probe->original_ink.height, probe->original_ink.x,
              probe->original_ink.y, probe->committed_ink.width, probe->committed_ink.height,
              probe->committed_ink.x, probe->committed_ink.y, probe->committed_mid_alpha_fraction);
  // Screen-vertical = the rotated text's width axis (where the 90% horizontal scale applies).
  CHECK(std::abs(probe->committed_ink.height - probe->original_ink.height) <=
        std::max(6, probe->original_ink.height / 25));
  // Screen-horizontal = the line-stack axis (fixed leading).
  CHECK(std::abs(probe->committed_ink.width - probe->original_ink.width) <=
        std::max(6, probe->original_ink.width / 25));
  CHECK(std::abs(probe->committed_ink.x - probe->original_ink.x) <= 8);
  CHECK(std::abs(probe->committed_ink.y - probe->original_ink.y) <= 8);

  // The back-panel savegame blurb: Arial Black at a UNIT-scale 90-degree rotation (no scale to
  // fold, a different path than the 1.284x layer above), fixed leading 35.42/27.08, tracking
  // -60..-100, H 90%. Reported repro: the converted block jumped up its reading axis
  // (screen-vertical) and the font combo showed Tahoma.
  const auto back_panel =
      run_photoshop_text_commit_probe(path, "Diese Spielkassette", 0.25, "ui_snes_back_panel_commit");
  if (!back_panel.has_value()) {
    return;
  }
  std::printf("  snes back panel ink: orig %dx%d at (%d,%d) -> committed %dx%d at (%d,%d)\n",
              back_panel->original_ink.width, back_panel->original_ink.height, back_panel->original_ink.x,
              back_panel->original_ink.y, back_panel->committed_ink.width, back_panel->committed_ink.height,
              back_panel->committed_ink.x, back_panel->committed_ink.y);
  CHECK(std::abs(back_panel->committed_ink.height - back_panel->original_ink.height) <=
        std::max(6, back_panel->original_ink.height / 25));
  CHECK(std::abs(back_panel->committed_ink.width - back_panel->original_ink.width) <= 8);
  CHECK(std::abs(back_panel->committed_ink.x - back_panel->original_ink.x) <= 8);
  // This layer reads BOTTOM-to-top: the anchored edge is the ink BOTTOM (line starts). Pinning
  // the document-visual top corner instead let the whole block slide up by any line-length
  // delta (the reported jump); the far end may float by the (small) length difference.
  CHECK(std::abs((back_panel->committed_ink.y + back_panel->committed_ink.height) -
                 (back_panel->original_ink.y + back_panel->original_ink.height)) <= 6);
}

void ui_restaurant_menu_other_layers_commit_match_if_available() {
  // The menu's other point-text shapes: 'Price' (right-justified, Justification 1 -- the tx
  // anchor is each line's END), 'Order Timing' (tiny 5.93 engine size under a strongly
  // non-uniform 7.14 x 5.47 transform -- the worst case for the old averaged scale), and
  // 'Additional Items' (paragraphs separated by empty lines whose runs carry their own
  // leading). Each converts via an unchanged edit -> apply and must keep Photoshop's band
  // structure.
  const auto path = patchy::test::local_psd_fixture_path("restaurant-menu-inside.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::Candara);

  {
    const auto probe = run_photoshop_text_commit_probe(path, "$ 270", 0.25, "ui_restaurant_menu_price_commit");
    if (probe.has_value()) {
      CHECK(probe->original_bands.size() == 6);
      CHECK(probe->committed_bands.size() == probe->original_bands.size());
      if (probe->committed_bands.size() == probe->original_bands.size()) {
        for (std::size_t i = 0; i < probe->original_bands.size(); ++i) {
          CHECK(std::abs(probe->committed_bands[i].bottom - probe->original_bands[i].bottom) <= 7);
        }
      }
      // Right-justified point text keeps its right edge near the type anchor. The crisp render
      // lays the substituted (non-condensed) glyphs out at the folded scale, so the edge can
      // drift ~2px of raw layout (~9px through the 4.5x transform); with the original font
      // installed it would be within a couple of pixels. The left edge is free to move: the
      // substitute is ~25% wider than Campanile.
      CHECK(std::abs((probe->committed_ink.x + probe->committed_ink.width) -
                     (probe->original_ink.x + probe->original_ink.width)) <= 12);
    }
  }
  {
    const auto probe = run_photoshop_text_commit_probe(path, "Order Served in Ten Minutes", 0.25,
                                                       "ui_restaurant_menu_order_timing_commit");
    if (probe.has_value()) {
      CHECK(probe->original_bands.size() == 1);
      CHECK(probe->committed_bands.size() == 1);
      if (!probe->original_bands.empty() && !probe->committed_bands.empty()) {
        CHECK(std::abs(probe->committed_bands[0].bottom - probe->original_bands[0].bottom) <= 6);
      }
      // Candara-BoldItalic is installed, so the 1.31x horizontal stretch must reproduce the
      // ink width closely (the old averaged transform scale rendered it ~18% too narrow).
      CHECK(std::abs(probe->committed_ink.width - probe->original_ink.width) <=
            std::max(8, probe->original_ink.width / 20));
    }
  }
  {
    // The dotted separator layer sits ON TOP of 'Additional Items' (clicking the menu column
    // activates it, Photoshop-style), so it is the layer this click flow converts: five
    // dash rows at fixed leading 30.00281 engine units (134.4 px through the transform).
    const auto probe = run_photoshop_text_commit_probe(path, "- - - -", 0.25,
                                                       "ui_restaurant_menu_separators_commit");
    if (probe.has_value()) {
      CHECK(probe->original_bands.size() == 5);
      CHECK(probe->committed_bands.size() == probe->original_bands.size());
      if (probe->committed_bands.size() == probe->original_bands.size()) {
        for (std::size_t i = 1; i < probe->original_bands.size(); ++i) {
          const auto original_advance =
              probe->original_bands[i].bottom - probe->original_bands[i - 1].bottom;
          const auto committed_advance =
              probe->committed_bands[i].bottom - probe->committed_bands[i - 1].bottom;
          CHECK(std::abs(committed_advance - original_advance) <= 4);
        }
      }
    }
  }
}

void ui_restaurant_menu_box_text_edit_commit_keeps_leading_if_available() {
  // The CHICKEN card: BOX text with a tight fixed leading on the headline (4.2135 engine units
  // -- smaller than the em, lines overlap by design), an empty spacer line (size 6, leading
  // 1.6), and an auto-leading description (8.71466 -> advances of 1.2 x 8.71 x 3.202 = ~33.5
  // px). Box sessions keep Photoshop's raster on an unchanged apply, so the probe types and
  // deletes a character to force a re-render of identical text. The description face
  // (OpenSans) is not installed, so wrapping can differ from the author's -- the assertions
  // pin the size-driven invariants: the description advance (auto leading depends only on the
  // size) and the headline's height (Candara-Bold is installed).
  const auto path = patchy::test::local_psd_fixture_path("restaurant-menu-inside.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::Candara);

  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::LayerId layer_id = 0;
  patchy::Rect layer_bounds{};
  std::vector<AlphaRowBand> original_bands;
  bool found = false;
  std::function<void(const std::vector<patchy::Layer>&)> find_chicken =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (!found) {
            if (const auto it = layer.metadata().find(patchy::kLayerMetadataText);
                it != layer.metadata().end() && it->second.find("CHICKEN") != std::string::npos) {
              layer_id = layer.id();
              layer_bounds = layer.bounds();
              original_bands = alpha_row_bands(layer.pixels());
              for (auto& band : original_bands) {
                band.top += layer.bounds().y;
                band.bottom += layer.bounds().y;
              }
              found = true;
            }
          }
          find_chicken(layer.children());
        }
      };
  find_chicken(document.layers());
  CHECK(found);
  if (!found) {
    return;
  }
  CHECK(original_bands.size() >= 3);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Chicken Card"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(0.5);
  QApplication::processEvents();

  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  live_document.set_active_layer(layer_id);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint click_doc(layer_bounds.x + layer_bounds.width / 2, layer_bounds.y + 10);
  const auto hit_point = canvas->widget_position_for_document_point(click_doc);
  accept_missing_psd_text_font_warning_if_present();
  send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(250);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->property("patchy.editingLayerId").toULongLong() == static_cast<qulonglong>(layer_id));
  // Type + delete: the text is unchanged but the session is marked edited, so applying
  // re-renders through Patchy's engine instead of keeping the source raster.
  auto cursor = editor->textCursor();
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  cursor.insertText(QStringLiteral("x"));
  QApplication::processEvents();
  cursor.deletePreviousChar();
  QApplication::processEvents();
  process_events_for(300);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(150);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  auto* committed = live_document.find_layer(layer_id);
  CHECK(committed != nullptr);
  if (committed == nullptr) {
    return;
  }
  CHECK(committed->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  auto committed_bands = alpha_row_bands(committed->pixels());
  for (auto& band : committed_bands) {
    band.top += committed->bounds().y;
    band.bottom += committed->bounds().y;
  }
  save_widget_artifact("ui_restaurant_menu_chicken_box_commit", *canvas);
  std::printf("  chicken: original raster %dx%d at (%d,%d), committed %dx%d at (%d,%d)\n",
              layer_bounds.width, layer_bounds.height, layer_bounds.x, layer_bounds.y,
              committed->pixels().width(), committed->pixels().height(), committed->bounds().x,
              committed->bounds().y);
  for (std::size_t i = 0; i < std::max(original_bands.size(), committed_bands.size()); ++i) {
    std::printf("    band %zu: orig %s committed %s\n", i,
                i < original_bands.size()
                    ? QStringLiteral("[%1,%2)").arg(original_bands[i].top).arg(original_bands[i].bottom).toUtf8().constData()
                    : "-",
                i < committed_bands.size()
                    ? QStringLiteral("[%1,%2)").arg(committed_bands[i].top).arg(committed_bands[i].bottom).toUtf8().constData()
                    : "-");
  }
  CHECK(committed_bands.size() == original_bands.size());
  if (committed_bands.size() != original_bands.size()) {
    return;
  }
  // Band bottoms ride the baselines. The box first baseline uses this machine's Candara-Bold
  // sTypoAscender while the author's raster reflects their font-era metrics, so allow a few
  // px; the glyph heights themselves are substitute-dependent and are not compared.
  for (std::size_t i = 0; i < original_bands.size(); ++i) {
    CHECK(std::abs(committed_bands[i].bottom - original_bands[i].bottom) <= 8);
  }
  CHECK(std::abs(committed_bands[0].top - original_bands[0].top) <= 14);
  // Description advances: auto leading = 1.2 x 8.71466 engine units x 3.202 = ~33.5 px,
  // independent of the substituted face.
  for (std::size_t i = 2; i < committed_bands.size(); ++i) {
    const auto advance = committed_bands[i].bottom - committed_bands[i - 1].bottom;
    CHECK(std::abs(advance - 34) <= 3);
  }
}

void ui_psd_sheared_point_text_edit_lands_on_glyphs() {
  // Regression (reported repro): mow_master.psd is a Photoshop CS-era file whose TySh descriptor
  // has no bounds/boundingBox fields.  Its sheared, center-justified "buttons" menu layer
  // therefore had no local glyph rect to align with, and the edit session fell back to the raw
  // transform origin -- the line-1 center anchor at the text baseline, ~(590, 277) -- while the
  // baked glyphs start at ~(547, 251).  The edit rect appeared off the text, and committing moved
  // the text to the wrong spot.  The document-space fallback pins the re-rendered glyphs to the
  // imported raster's visible bounds instead.
  const auto path = patchy::test::local_psd_fixture_path("mow_master.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  // The layer uses Arial Black.  ariblk.ttf's typographic family is "Arial" (subfamily "Black"),
  // so the offscreen FreeType database exposes it as a style of Arial rather than as an
  // "Arial Black" family; available_text_family_style_match() resolves that, so registering the
  // file is enough for the installed-font path (no substitution warning).  If the font is absent
  // the substitution path runs instead -- the position assertions hold either way because the
  // document-space pin keeps the block center and visible top exact for any face.
  register_test_fonts(TestFontRole::ArialBlack);
  const bool arial_black_available =
      QFontDatabase::families().contains(QStringLiteral("Arial Black")) ||
      QFontDatabase::styles(QStringLiteral("Arial")).contains(QStringLiteral("Black"));
  auto document = patchy::psd::DocumentIo::read_file(path);

  patchy::Rect text_bounds{};
  patchy::LayerId buttons_id = 0;
  bool found = false;
  std::function<void(const std::vector<patchy::Layer>&)> find_buttons =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (!found && layer.name() == "buttons" && layer.visible() &&
              layer.metadata().contains(patchy::kLayerMetadataText)) {
            text_bounds = layer.bounds();
            buttons_id = layer.id();
            found = true;
          }
          find_buttons(layer.children());
        }
      };
  find_buttons(document.layers());
  CHECK(found);
  if (!found) {
    return;  // fixture layout changed; nothing to assert against
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Mow Master Menu"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();

  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* buttons_before = live_document.find_layer(buttons_id);
  CHECK(buttons_before != nullptr);
  const auto source_visible =
      alpha_pixel_bounds_in_rows(buttons_before->pixels(), 0, buttons_before->pixels().height());
  CHECK(source_visible.has_value());
  if (!source_visible.has_value()) {
    return;
  }
  const QRect source_doc(text_bounds.x + source_visible->left(), text_bounds.y + source_visible->top(),
                         source_visible->width(), source_visible->height());
  const auto source_center_x = source_doc.left() + source_doc.width() / 2.0;

  live_document.set_active_layer(buttons_id);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint click_doc(source_doc.left() + source_doc.width() / 2, source_doc.top() + 16);
  // Record whether the missing-font warning appeared (and accept it so the test can proceed
  // when the font genuinely is unavailable).
  auto missing_font_dialog_seen = std::make_shared<bool>(false);
  QTimer::singleShot(0, [missing_font_dialog_seen] {
    auto* dialog =
        qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("missingPsdTextFontMessageBox")));
    if (dialog == nullptr) {
      return;
    }
    *missing_font_dialog_seen = true;
    for (auto* button : dialog->findChildren<QPushButton*>()) {
      if (dialog->buttonRole(button) == QMessageBox::AcceptRole) {
        button->click();
        return;
      }
    }
  });
  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(click_doc),
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(click_doc),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(250);

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->toPlainText().contains(QStringLiteral("Options")));
  // The session shows the live render from entry (no waiting for the first keystroke).
  CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
  save_widget_artifact("ui_mow_master_buttons_editing", *canvas);

  auto cursor = editor->textCursor();
  cursor.movePosition(QTextCursor::End);
  editor->setTextCursor(cursor);
  cursor.insertText(QStringLiteral("!"));
  QApplication::processEvents();
  process_events_for(300);

  // The live preview must land on the imported glyphs, not at the raw transform origin (which is
  // ~42px right of and ~26px below the visible glyph top for this layer).  The block is
  // center-justified, so the pin keeps the visible center and top regardless of the face used.
  if (auto* preview = preview_layer_for_editor(live_document, *editor); preview != nullptr) {
    const auto preview_visible =
        alpha_pixel_bounds_in_rows(preview->pixels(), 0, preview->pixels().height());
    CHECK(preview_visible.has_value());
    if (preview_visible.has_value()) {
      const auto preview_center_x = preview->bounds().x + preview_visible->left() +
                                    preview_visible->width() / 2.0;
      CHECK(std::abs(preview_center_x - source_center_x) <= 10.0);
      CHECK(std::abs(preview->bounds().y + preview_visible->top() - source_doc.top()) <= 8);
    }
  }

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  process_events_for(100);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  save_widget_artifact("ui_mow_master_buttons_committed", *canvas);

  // Safe to assert dialog behavior now that the editor is closed (a failing CHECK while an inline
  // editor is alive aborts during unwind).  Registering ariblk.ttf must satisfy the availability
  // check through the family+style resolution -- no substitution warning.
  if (arial_black_available) {
    CHECK(!*missing_font_dialog_seen);
  }

  auto* buttons_after = live_document.find_layer(buttons_id);
  CHECK(buttons_after != nullptr);
  if (buttons_after == nullptr) {
    return;
  }
  // The PSD's own layer name is not auto-derived from the text, so the commit must keep it.
  CHECK(buttons_after->name() == "buttons");
  const auto committed_visible =
      alpha_pixel_bounds_in_rows(buttons_after->pixels(), 0, buttons_after->pixels().height());
  CHECK(committed_visible.has_value());
  if (!committed_visible.has_value()) {
    return;
  }
  const QRect committed_doc(buttons_after->bounds().x + committed_visible->left(),
                            buttons_after->bounds().y + committed_visible->top(),
                            committed_visible->width(), committed_visible->height());
  // The committed glyph block stays pinned to the imported raster: centered horizontally (the
  // layer is center-justified) and at the same visible top.
  CHECK(std::abs(committed_doc.left() + committed_doc.width() / 2.0 - source_center_x) <= 10.0);
  CHECK(std::abs(committed_doc.top() - source_doc.top()) <= 8);
}

void ui_duke_psd_text_runs_survive_reedit() {
  // Regression (reported repro): Duke nukem mobile.psd is a 2004-era file whose text uses \x03
  // control characters as line terminators -- the "I did all the programming..." layer's text
  // BEGINS with one, importing as a leading blank line.  Committing stored the layer text
  // trimmed() while the rich-text/paragraph runs kept full-document offsets, so the next edit
  // session applied every run shifted: colors/sizes broke mid-word and selection highlights
  // landed off the glyphs.  The text is now stored untrimmed and the rasterizer builds from the
  // same plain-text + runs construction the editor and caret layout use, so an edit/apply cycle
  // must be a fixed point: applying twice with no text change leaves metadata, bounds, and pixels
  // identical.
  const auto path = patchy::test::local_psd_fixture_path("Duke nukem mobile.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }
  auto document = patchy::psd::DocumentIo::read_file(path);

  patchy::Rect text_bounds{};
  patchy::LayerId body_id = 0;
  bool found = false;
  std::function<void(const std::vector<patchy::Layer>&)> find_body =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (!found) {
            if (const auto it = layer.metadata().find(patchy::kLayerMetadataText);
                it != layer.metadata().end() &&
                it->second.find("I did all the programming") != std::string::npos && layer.visible()) {
              text_bounds = layer.bounds();
              body_id = layer.id();
              found = true;
            }
          }
          find_body(layer.children());
        }
      };
  find_body(document.layers());
  if (!found) {
    return;  // fixture layout changed; nothing to assert against
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Duke Text Reedit"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(0.25);
  QApplication::processEvents();

  auto& live_document = patchy::ui::MainWindowTestAccess::document(window);
  auto* body_before = live_document.find_layer(body_id);
  CHECK(body_before != nullptr);
  if (body_before == nullptr) {
    return;
  }
  const auto import_text = QString::fromStdString(body_before->metadata().at(patchy::kLayerMetadataText));
  // The PSD's leading \x03 imports as a leading blank line; it must survive the whole cycle.
  CHECK(import_text.startsWith(QLatin1Char('\n')));

  const auto metadata_value = [&live_document, body_id](const char* key) {
    auto* layer = live_document.find_layer(body_id);
    if (layer == nullptr) {
      return QString();
    }
    const auto found_value = layer->metadata().find(key);
    return found_value != layer->metadata().end() ? QString::fromStdString(found_value->second) : QString();
  };

  const auto run_session = [&](const char* artifact_name) {
    live_document.set_active_layer(body_id);
    require_action_by_text(window, QStringLiteral("Type"))->trigger();
    const QPoint click_doc(text_bounds.x + text_bounds.width / 2, text_bounds.y + text_bounds.height / 2);
    const auto hit_point = canvas->widget_position_for_document_point(click_doc);
    accept_missing_psd_text_font_warning_if_present();
    send_mouse(*canvas, QEvent::MouseButtonPress, hit_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, hit_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    process_events_for(250);
    auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    CHECK(editor != nullptr);
    if (editor == nullptr) {
      return false;
    }
    CHECK(editor->toPlainText().contains(QStringLiteral("I did all the programming")));
    // The session shows the live render from entry, so selection/caret geometry (computed from
    // Patchy's layout) matches the on-screen glyphs even before the first keystroke.
    CHECK(!editor->property("patchy.sourceRasterPreview").toBool());
    CHECK(editor->property("patchy.previewPaintsText").toBool());
    CHECK(preview_layer_for_editor(live_document, *editor) != nullptr);
    if (artifact_name != nullptr) {
      save_widget_artifact(artifact_name, *canvas);
    }
    require_action_by_text(window, QStringLiteral("Move"))->trigger();
    QApplication::processEvents();
    process_events_for(120);
    CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
    return true;
  };

  if (!run_session("ui_duke_text_first_apply")) {
    return;
  }
  auto* body_first = live_document.find_layer(body_id);
  CHECK(body_first != nullptr);
  if (body_first == nullptr) {
    return;
  }
  const auto text_first = metadata_value(patchy::kLayerMetadataText);
  const auto runs_first = metadata_value(patchy::kLayerMetadataTextRuns);
  const auto paragraph_runs_first = metadata_value(patchy::kLayerMetadataTextParagraphRuns);
  const auto bounds_first = body_first->bounds();
  const std::vector<std::uint8_t> pixels_first(body_first->pixels().data().begin(),
                                               body_first->pixels().data().end());
  // The committed text keeps the leading blank line so the run offsets stay valid.
  CHECK(text_first.startsWith(QLatin1Char('\n')));
  CHECK(metadata_value(patchy::kLayerMetadataTextRasterStatus) == QStringLiteral("patchy_raster"));

  if (!run_session("ui_duke_text_second_apply")) {
    return;
  }
  auto* body_second = live_document.find_layer(body_id);
  CHECK(body_second != nullptr);
  if (body_second == nullptr) {
    return;
  }
  // The corruption detector: re-applying with no text change must leave the stored text and run
  // offsets identical (the trim bug shifted every run here).
  CHECK(metadata_value(patchy::kLayerMetadataText) == text_first);
  CHECK(metadata_value(patchy::kLayerMetadataTextRuns) == runs_first);
  CHECK(metadata_value(patchy::kLayerMetadataTextParagraphRuns) == paragraph_runs_first);
  // The first apply hands the layer from Photoshop's raster to Patchy's renderer: this box layer
  // drops the one-time metric calibration that squeezed the layout toward Photoshop's raster, so
  // the second apply may reflow slightly (a couple percent).  Stay in the neighborhood...
  CHECK(std::abs(body_second->bounds().x - bounds_first.x) <= 64);
  CHECK(std::abs(body_second->bounds().y - bounds_first.y) <= 32);
  const auto bounds_second = body_second->bounds();
  const std::vector<std::uint8_t> pixels_second(body_second->pixels().data().begin(),
                                                body_second->pixels().data().end());

  // ...and be byte-for-byte identical on the next apply.
  if (!run_session(nullptr)) {
    return;
  }
  auto* body_third = live_document.find_layer(body_id);
  CHECK(body_third != nullptr);
  if (body_third == nullptr) {
    return;
  }
  CHECK(metadata_value(patchy::kLayerMetadataText) == text_first);
  CHECK(metadata_value(patchy::kLayerMetadataTextRuns) == runs_first);
  CHECK(body_third->bounds().x == bounds_second.x);
  CHECK(body_third->bounds().y == bounds_second.y);
  CHECK(body_third->bounds().width == bounds_second.width);
  CHECK(body_third->bounds().height == bounds_second.height);
  const std::vector<std::uint8_t> pixels_third(body_third->pixels().data().begin(),
                                               body_third->pixels().data().end());
  CHECK(pixels_third == pixels_second);

  // After a real keystroke the painted caret must still sit on the rendered glyphs (reported
  // repro: deleting/adding a letter made the caret/selection drift off the text).
  live_document.set_active_layer(body_id);
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint probe_click(body_third->bounds().x + body_third->bounds().width / 2,
                           body_third->bounds().y + body_third->bounds().height / 2);
  send_mouse(*canvas, QEvent::MouseButtonPress, canvas->widget_position_for_document_point(probe_click),
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, canvas->widget_position_for_document_point(probe_click),
             Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  process_events_for(250);
  auto* probe_editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(probe_editor != nullptr);
  if (probe_editor == nullptr) {
    return;
  }
  auto probe_cursor = probe_editor->textCursor();
  probe_cursor.movePosition(QTextCursor::End);
  probe_editor->setTextCursor(probe_cursor);
  probe_cursor.insertText(QStringLiteral("x"));
  QApplication::processEvents();
  process_events_for(350);
  auto* probe_preview = preview_layer_for_editor(live_document, *probe_editor);
  CHECK(probe_preview != nullptr);
  if (probe_preview != nullptr) {
    const auto probe_bands = alpha_row_bands(probe_preview->pixels());
    CHECK(probe_bands.size() >= 4U);
    const auto zoom = canvas->zoom();
    const auto editor_doc_y = probe_editor->property("patchy.documentTextY").toInt();
    const auto caret_in_some_band = [&](const char* label) {
      QApplication::processEvents();
      const auto caret_rect = probe_editor->property("patchy.previewCaretRect").toRect();
      CHECK(!caret_rect.isEmpty());
      if (caret_rect.isEmpty()) {
        return;
      }
      const auto caret_center = (caret_rect.top() + caret_rect.bottom()) / 2.0;
      bool inside = false;
      double nearest = 1e9;
      for (const auto& band : probe_bands) {
        const auto top = (static_cast<double>(probe_preview->bounds().y + band.top) - editor_doc_y) * zoom;
        const auto bottom = (static_cast<double>(probe_preview->bounds().y + band.bottom) - editor_doc_y) * zoom;
        const auto pad = std::max(2.0, (bottom - top) * 0.4);
        if (caret_center > top - pad && caret_center < bottom + pad) {
          inside = true;
        }
        nearest = std::min(nearest, std::min(std::abs(caret_center - top), std::abs(caret_center - bottom)));
      }
      Q_UNUSED(label);
      Q_UNUSED(nearest);
      CHECK(inside);
    };
    const auto plain = probe_editor->toPlainText();
    const auto ask_index = plain.indexOf(QStringLiteral("Ask me"));
    CHECK(ask_index >= 0);
    if (ask_index >= 0) {
      auto mid_cursor = probe_editor->textCursor();
      mid_cursor.setPosition(static_cast<int>(ask_index) + 2);
      probe_editor->setTextCursor(mid_cursor);
      caret_in_some_band("Ask-line");
    }
    const auto did_index = plain.indexOf(QStringLiteral("I did all"));
    CHECK(did_index >= 0);
    if (did_index >= 0) {
      auto top_cursor = probe_editor->textCursor();
      top_cursor.setPosition(static_cast<int>(did_index) + 2);
      probe_editor->setTextCursor(top_cursor);
      caret_in_some_band("I-did-line");
    }
  }
  send_key(*probe_editor, Qt::Key_Escape);
  QApplication::processEvents();
}

void ui_text_tool_commits_rich_text_spans() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint text_document_point(96, 96);
  const auto text_widget_point = canvas->widget_position_for_document_point(text_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->styleSheet().contains(QStringLiteral("selection-color: rgb(")));
  CHECK(!editor->styleSheet().contains(QStringLiteral("font-size:")));
  editor->setHtml(QStringLiteral(
      "<html><body><p style='margin:0px;'>"
      "<span style='font-family:Arial; font-size:56px; color:#e02020;'>Red </span>"
      "<span style='font-family:Times New Roman; font-size:56px; color:#2050f0; font-weight:700; font-style:italic;'>Blue</span>"
      "</p></body></html>"));
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);

  const auto image = canvas->grab().toImage();
  bool saw_red = false;
  bool saw_blue = false;
  for (int y = 0; y < 110 && (!saw_red || !saw_blue); y += 2) {
    for (int x = 0; x < 360 && (!saw_red || !saw_blue); x += 2) {
      const auto widget_point = canvas->widget_position_for_document_point(text_document_point + QPoint(x, y));
      if (!image.rect().contains(widget_point)) {
        continue;
      }
      const auto color = image.pixelColor(widget_point);
      saw_red = saw_red || (color.red() > 150 && color.green() < 100 && color.blue() < 100);
      saw_blue = saw_blue || (color.blue() > 150 && color.red() < 100 && color.green() < 130);
    }
  }
  CHECK(saw_red);
  CHECK(saw_blue);
  save_widget_artifact("ui_text_tool_rich_text_spans", window);
}

void ui_text_options_follow_active_rich_text_span() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto text_widget_point = canvas->widget_position_for_document_point(QPoint(96, 96));
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* text_bold = window.findChild<QPushButton*>(QStringLiteral("textBoldButton"));
  auto* text_italic = window.findChild<QPushButton*>(QStringLiteral("textItalicButton"));
  auto* text_color = window.findChild<QPushButton*>(QStringLiteral("textColorButton"));
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(editor != nullptr);
  CHECK(text_size != nullptr);
  CHECK(text_bold != nullptr);
  CHECK(text_italic != nullptr);
  CHECK(text_color != nullptr);
  CHECK(foreground != nullptr);

  editor->setHtml(QStringLiteral(
      "<html><body><p style='margin:0px;'>"
      "<span style='font-family:Arial; font-size:24px; color:#e02020;'>Red </span>"
      "<span style='font-family:Times New Roman; font-size:72px; color:#2050f0; font-weight:700; font-style:italic;'>Blue</span>"
      "</p></body></html>"));
  QApplication::processEvents();

  QTextCursor cursor(editor->document());
  cursor.setPosition(1);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  CHECK(std::abs(text_size->value() - text_points_for_pixels(24)) < 0.01);
  CHECK(!text_bold->isChecked());
  CHECK(!text_italic->isChecked());

  cursor.select(QTextCursor::Document);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  CHECK(!text_bold->isChecked());
  text_bold->click();
  QApplication::processEvents();

  QTextCursor red_after_bold(editor->document());
  red_after_bold.setPosition(1);
  red_after_bold.setPosition(2, QTextCursor::KeepAnchor);
  const auto red_bold_format = red_after_bold.charFormat();
  CHECK(red_bold_format.font().pixelSize() == 24);
  CHECK(red_bold_format.font().family().contains(QStringLiteral("Arial"), Qt::CaseInsensitive));
  CHECK(red_bold_format.font().bold());
  CHECK(red_bold_format.foreground().color() == QColor(224, 32, 32));

  QTextCursor blue_after_bold(editor->document());
  const auto initial_blue_start = editor->toPlainText().indexOf(QStringLiteral("Blue"));
  blue_after_bold.setPosition(initial_blue_start);
  blue_after_bold.setPosition(initial_blue_start + 1, QTextCursor::KeepAnchor);
  const auto blue_bold_format = blue_after_bold.charFormat();
  CHECK(blue_bold_format.font().pixelSize() == 72);
  CHECK(blue_bold_format.font().family().contains(QStringLiteral("Times"), Qt::CaseInsensitive));
  CHECK(blue_bold_format.font().bold());
  CHECK(blue_bold_format.font().italic());
  CHECK(blue_bold_format.foreground().color() == QColor(32, 80, 240));

  cursor.setPosition(editor->toPlainText().indexOf(QStringLiteral("Blue")));
  cursor.setPosition(cursor.position() + 4, QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  CHECK(std::abs(text_size->value() - text_points_for_pixels(72)) < 0.01);
  CHECK(text_bold->isChecked());
  CHECK(text_italic->isChecked());
  CHECK(editor->property("patchy.documentTextColor").value<QColor>() == QColor(32, 80, 240));

  text_color->click();
  QApplication::processEvents();
  bool changed_text_color = false;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
      continue;
    }
    auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
    CHECK(picker != nullptr);
    picker->setCurrentColor(QColor(20, 180, 90));
    QApplication::processEvents();
    widget->close();
    changed_text_color = true;
    break;
  }
  CHECK(changed_text_color);
  CHECK(editor->textCursor().hasSelection());
  CHECK(editor->styleSheet().contains(QStringLiteral("selection-color: rgb(20, 180, 90)")));

  QTextCursor blue_probe(editor->document());
  const auto blue_start = editor->toPlainText().indexOf(QStringLiteral("Blue"));
  blue_probe.setPosition(blue_start);
  blue_probe.setPosition(blue_start + 1, QTextCursor::KeepAnchor);
  const auto blue_format = blue_probe.charFormat();
  CHECK(blue_format.font().pixelSize() == 72);
  CHECK(blue_format.font().bold());
  CHECK(blue_format.font().italic());
  CHECK(blue_format.foreground().color() == QColor(20, 180, 90));

  QTextCursor red_probe(editor->document());
  red_probe.setPosition(1);
  red_probe.setPosition(2, QTextCursor::KeepAnchor);
  const auto red_format = red_probe.charFormat();
  CHECK(red_format.font().pixelSize() == 24);
  CHECK(red_format.foreground().color() == QColor(224, 32, 32));

  cursor.setPosition(blue_start);
  cursor.setPosition(blue_start + 4, QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  QApplication::processEvents();
  foreground->click();
  QApplication::processEvents();
  bool changed_foreground_color = false;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() != QStringLiteral("patchyColorDialog") || !widget->isVisible()) {
      continue;
    }
    auto* picker = widget->findChild<patchy::ui::PatchyColorPicker*>(QStringLiteral("patchyAdvancedColorPicker"));
    CHECK(picker != nullptr);
    picker->setCurrentColor(QColor(140, 70, 220));
    QApplication::processEvents();
    widget->close();
    changed_foreground_color = true;
    break;
  }
  CHECK(changed_foreground_color);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor);
  CHECK(editor->textCursor().hasSelection());
  CHECK(editor->property("patchy.documentTextColor").value<QColor>() == QColor(140, 70, 220));
  CHECK(canvas->primary_color() == QColor(140, 70, 220));

  QTextCursor foreground_blue_probe(editor->document());
  foreground_blue_probe.setPosition(blue_start);
  foreground_blue_probe.setPosition(blue_start + 1, QTextCursor::KeepAnchor);
  CHECK(foreground_blue_probe.charFormat().foreground().color() == QColor(140, 70, 220));

  send_key(*editor, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
}

}  // namespace

void ui_cli_append_text_rerenders_and_roundtrips() {
  // The --append-text half of the CLI export automation (MainWindow::run_cli_export):
  // every text layer gains the suffix through a real inline-editor session, re-renders
  // through Patchy's text engine (raster status becomes patchy_raster), and the
  // automation-mode saves land without any prompt. The Testy compatibility harness
  // (testy/) depends on this contract.
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto path = patchy::test::committed_psd_fixture_path("photoshop-text-point-fixed-leading.psd");
  auto document = patchy::psd::DocumentIo::read_file(path);

  std::size_t text_layer_count = 0;
  patchy::LayerId text_layer_id = 0;
  std::function<void(const std::vector<patchy::Layer>&)> count_text_layers =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (patchy::layer_is_text(layer)) {
            ++text_layer_count;
            text_layer_id = layer.id();
          }
          count_text_layers(layer.children());
        }
      };
  count_text_layers(document.layers());
  CHECK(text_layer_count >= 1);
  if (text_layer_count == 0) {
    return;
  }

  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  window.add_document_session(std::move(document), QStringLiteral("CLI Append Text"));
  require_canvas(window);

  const auto& live_document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* original = live_document.find_layer(text_layer_id);
  CHECK(original != nullptr);
  if (original == nullptr) {
    return;
  }
  const auto original_pixels = original->pixels();
  const auto original_width = original->bounds().width;

  const auto suffix = QStringLiteral("~TESTY~");
  const int mutated = patchy::ui::MainWindowTestAccess::cli_append_text_to_text_layers(window, suffix);
  CHECK(static_cast<std::size_t>(mutated) == text_layer_count);

  const auto* appended = live_document.find_layer(text_layer_id);
  CHECK(appended != nullptr);
  if (appended == nullptr) {
    return;
  }
  const auto stored_text = QString::fromStdString(appended->metadata().at(patchy::kLayerMetadataText));
  CHECK(stored_text.endsWith(suffix));
  CHECK(appended->metadata().at(patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  // The suffix lengthens the last line, so the re-rendered raster cannot equal the import.
  const auto appended_data = appended->pixels().data();
  const auto original_data = original_pixels.data();
  const bool pixels_differ =
      appended_data.size() != original_data.size() ||
      !std::equal(appended_data.begin(), appended_data.end(), original_data.begin());
  CHECK(appended->bounds().width > original_width || pixels_differ);

  QTemporaryDir temp_dir;
  CHECK(temp_dir.isValid());
  const auto psd_path = temp_dir.filePath(QStringLiteral("cli_append_roundtrip.psd"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, psd_path,
                                                                patchy::ui::ImageSaveOptions{}));
  const auto png_path = temp_dir.filePath(QStringLiteral("cli_append_flat.png"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, png_path,
                                                                patchy::ui::ImageSaveOptions{}));
  CHECK(QFileInfo::exists(png_path));
  CHECK(QFileInfo(png_path).size() > 0);

  // Round trip: the appended text must survive a PSD save + reopen (fresh runtime ids, so
  // find the text layer by walking).
  const auto reread = patchy::psd::DocumentIo::read_file(psd_path.toStdString());
  bool roundtrip_found = false;
  std::function<void(const std::vector<patchy::Layer>&)> check_roundtrip =
      [&](const std::vector<patchy::Layer>& layers) {
        for (const auto& layer : layers) {
          if (patchy::layer_is_text(layer)) {
            const auto text = QString::fromStdString(layer.metadata().at(patchy::kLayerMetadataText));
            if (text.endsWith(suffix)) {
              roundtrip_found = true;
            }
          }
          check_roundtrip(layer.children());
        }
      };
  check_roundtrip(reread.layers());
  CHECK(roundtrip_found);
}

std::vector<patchy::test::TestCase> text_transform_commit_tests_part2() {
  return {
      {"ui_psd_centered_point_text_keeps_center_on_commit",
       ui_psd_centered_point_text_keeps_center_on_commit},
      {"ui_psd_text_fixed_leading_commit_matches_photoshop_row_bands",
       ui_psd_text_fixed_leading_commit_matches_photoshop_row_bands},
      {"ui_psd_text_auto_leading_commit_matches_photoshop_row_bands",
       ui_psd_text_auto_leading_commit_matches_photoshop_row_bands},
      {"ui_psd_text_transformed_commit_keeps_photoshop_leading",
       ui_psd_text_transformed_commit_keeps_photoshop_leading},
      {"ui_psd_text_box_and_tracking_rasterize_match_photoshop",
       ui_psd_text_box_and_tracking_rasterize_match_photoshop},
      {"ui_psd_text_hv_scale_rasterize_matches_photoshop",
       ui_psd_text_hv_scale_rasterize_matches_photoshop},
      {"ui_snes_box_rotated_hscale_commit_matches_if_available",
       ui_snes_box_rotated_hscale_commit_matches_if_available},
      {"ui_restaurant_menu_dishes_commit_matches_photoshop_bands_if_available",
       ui_restaurant_menu_dishes_commit_matches_photoshop_bands_if_available},
      {"ui_restaurant_menu_other_layers_commit_match_if_available",
       ui_restaurant_menu_other_layers_commit_match_if_available},
      {"ui_restaurant_menu_box_text_edit_commit_keeps_leading_if_available",
       ui_restaurant_menu_box_text_edit_commit_keeps_leading_if_available},
      {"ui_psd_sheared_point_text_edit_lands_on_glyphs",
       ui_psd_sheared_point_text_edit_lands_on_glyphs},
      {"ui_duke_psd_text_runs_survive_reedit", ui_duke_psd_text_runs_survive_reedit},
      {"ui_cli_append_text_rerenders_and_roundtrips", ui_cli_append_text_rerenders_and_roundtrips},
      {"ui_text_tool_commits_rich_text_spans", ui_text_tool_commits_rich_text_spans},
      {"ui_text_options_follow_active_rich_text_span",
       ui_text_options_follow_active_rich_text_span},
  };
}
